/*
* This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __NETWORK_THREAD_HPP_
#define __NETWORK_THREAD_HPP_

#include <thread>
#include <list>
#include <mutex>
#include <chrono>
#include <atomic>
#include <utility>

#include <boost/asio.hpp>

#include "Socket.hpp"

namespace MaNGOS
{
    template <typename SocketType>
    class NetworkThread
    {
        private:
            const int WorkDelay = 500;

            boost::asio::io_service m_service;

            std::mutex m_socketLock;
            std::list<std::unique_ptr<SocketType>> m_sockets;

            // note that the work member *must* be declared after the service member for the work constructor to function correctly
            boost::asio::io_service::work m_work;

            std::mutex m_closingSocketLock;
            std::list<std::unique_ptr<SocketType>> m_closingSockets;

            std::atomic<bool> m_pendingShutdown;

            std::thread m_serviceThread;
            std::thread m_socketCleanupThread;

            void SocketCleanupWork();

        public:
            NetworkThread() : m_work(m_service), m_pendingShutdown(false),
                m_serviceThread([this] { boost::system::error_code ec; this->m_service.run(ec); }),
                m_socketCleanupThread([this] { this->SocketCleanupWork(); })
            {
                m_serviceThread.detach();
            }

            ~NetworkThread()
            {
                m_pendingShutdown = true;
                m_socketCleanupThread.join();
            }

            size_t Size() const { return m_sockets.size(); }

            SocketType *CreateSocket();

            void RemoveSocket(Socket *socket)
            {
                std::lock_guard<std::mutex> guard(m_socketLock);
                std::lock_guard<std::mutex> closingGuard(m_closingSocketLock);

                for (auto i = m_sockets.begin(); i != m_sockets.end(); ++i)
                    if (i->get() == socket)
                    {
                        m_closingSockets.push_front(std::move(*i));
                        return;
                    }
            }
    };

    template <typename SocketType>
    void NetworkThread<SocketType>::SocketCleanupWork()
    {
        while (!m_pendingShutdown)
        {
            {
                std::lock_guard<std::mutex> guard(m_closingSocketLock);

                for (auto i = m_closingSockets.begin(); i != m_closingSockets.end(); i++)
                    if (i->get()->Deletable())
                        i = m_closingSockets.erase(i);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(WorkDelay));
        }
    }

    template <typename SocketType>
    SocketType *NetworkThread<SocketType>::CreateSocket()
    {
        std::lock_guard<std::mutex> guard(m_socketLock);

        m_sockets.push_front(std::unique_ptr<SocketType>(new SocketType(m_service, [this](Socket *socket) { this->RemoveSocket(socket); })));

        return m_sockets.begin()->get();
    }
}

#endif /* !__NETWORK_THREAD_HPP_ */