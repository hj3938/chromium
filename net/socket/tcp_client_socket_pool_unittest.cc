// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/tcp_client_socket_pool.h"

#include "base/compiler_specific.h"
#include "base/message_loop.h"
#include "net/base/mock_host_resolver.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/socket/client_socket.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/socket_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

const int kMaxSockets = 32;
const int kMaxSocketsPerGroup = 6;
const int kDefaultPriority = 5;

class MockClientSocket : public ClientSocket {
 public:
  MockClientSocket() : connected_(false) {}

  // ClientSocket methods:
  virtual int Connect(CompletionCallback* callback) {
    connected_ = true;
    return OK;
  }
  virtual void Disconnect() {
    connected_ = false;
  }
  virtual bool IsConnected() const {
    return connected_;
  }
  virtual bool IsConnectedAndIdle() const {
    return connected_;
  }

  // Socket methods:
  virtual int Read(IOBuffer* buf, int buf_len,
                   CompletionCallback* callback) {
    return ERR_FAILED;
  }
  virtual int Write(IOBuffer* buf, int buf_len,
                    CompletionCallback* callback) {
    return ERR_FAILED;
  }

 private:
  bool connected_;
};

class MockFailingClientSocket : public ClientSocket {
 public:
  MockFailingClientSocket() {}

  // ClientSocket methods:
  virtual int Connect(CompletionCallback* callback) {
    return ERR_CONNECTION_FAILED;
  }

  virtual void Disconnect() {}

  virtual bool IsConnected() const {
    return false;
  }
  virtual bool IsConnectedAndIdle() const {
    return false;
  }

  // Socket methods:
  virtual int Read(IOBuffer* buf, int buf_len,
                   CompletionCallback* callback) {
    return ERR_FAILED;
  }

  virtual int Write(IOBuffer* buf, int buf_len,
                    CompletionCallback* callback) {
    return ERR_FAILED;
  }
};

class MockPendingClientSocket : public ClientSocket {
 public:
  MockPendingClientSocket(bool should_connect)
      : method_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
        should_connect_(should_connect),
        is_connected_(false) {}

  // ClientSocket methods:
  virtual int Connect(CompletionCallback* callback) {
    MessageLoop::current()->PostTask(
        FROM_HERE,
        method_factory_.NewRunnableMethod(
           &MockPendingClientSocket::DoCallback, callback));
    return ERR_IO_PENDING;
  }

  virtual void Disconnect() {}

  virtual bool IsConnected() const {
    return is_connected_;
  }
  virtual bool IsConnectedAndIdle() const {
    return is_connected_;
  }

  // Socket methods:
  virtual int Read(IOBuffer* buf, int buf_len,
                   CompletionCallback* callback) {
    return ERR_FAILED;
  }

  virtual int Write(IOBuffer* buf, int buf_len,
                    CompletionCallback* callback) {
    return ERR_FAILED;
  }

 private:
  void DoCallback(CompletionCallback* callback) {
    if (should_connect_) {
      is_connected_ = true;
      callback->Run(OK);
    } else {
      is_connected_ = false;
      callback->Run(ERR_CONNECTION_FAILED);
    }
  }

  ScopedRunnableMethodFactory<MockPendingClientSocket> method_factory_;
  bool should_connect_;
  bool is_connected_;
};

class MockClientSocketFactory : public ClientSocketFactory {
 public:
  enum ClientSocketType {
    MOCK_CLIENT_SOCKET,
    MOCK_FAILING_CLIENT_SOCKET,
    MOCK_PENDING_CLIENT_SOCKET,
    MOCK_PENDING_FAILING_CLIENT_SOCKET,
  };

  MockClientSocketFactory()
      : allocation_count_(0), client_socket_type_(MOCK_CLIENT_SOCKET) {}

  virtual ClientSocket* CreateTCPClientSocket(const AddressList& addresses) {
    allocation_count_++;
    switch (client_socket_type_) {
      case MOCK_CLIENT_SOCKET:
        return new MockClientSocket();
      case MOCK_FAILING_CLIENT_SOCKET:
        return new MockFailingClientSocket();
      case MOCK_PENDING_CLIENT_SOCKET:
        return new MockPendingClientSocket(true);
      case MOCK_PENDING_FAILING_CLIENT_SOCKET:
        return new MockPendingClientSocket(false);
      default:
        NOTREACHED();
        return new MockClientSocket();
    }
  }

  virtual SSLClientSocket* CreateSSLClientSocket(
      ClientSocket* transport_socket,
      const std::string& hostname,
      const SSLConfig& ssl_config) {
    NOTIMPLEMENTED();
    return NULL;
  }

  int allocation_count() const { return allocation_count_; }

  void set_client_socket_type(ClientSocketType type) {
    client_socket_type_ = type;
  }

 private:
  int allocation_count_;
  ClientSocketType client_socket_type_;
};

class TCPClientSocketPoolTest : public ClientSocketPoolTest {
 protected:
  TCPClientSocketPoolTest()
      : host_resolver_(new MockHostResolver),
        pool_(new TCPClientSocketPool(kMaxSockets,
                                      kMaxSocketsPerGroup,
                                      host_resolver_,
                                      &client_socket_factory_)) {
  }

  int StartRequest(const std::string& group_name, int priority) {
    return StartRequestUsingPool(pool_.get(), group_name, priority);
  }

  scoped_refptr<MockHostResolver> host_resolver_;
  MockClientSocketFactory client_socket_factory_;
  scoped_refptr<ClientSocketPool> pool_;
};

TEST_F(TCPClientSocketPoolTest, Basic) {
  TestCompletionCallback callback;
  ClientSocketHandle handle(pool_.get());
  HostResolver::RequestInfo info("www.google.com", 80);
  int rv = handle.Init("a", info, 0, &callback, NULL);
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle.is_initialized());
  EXPECT_FALSE(handle.socket());

  EXPECT_EQ(OK, callback.WaitForResult());
  EXPECT_TRUE(handle.is_initialized());
  EXPECT_TRUE(handle.socket());

  handle.Reset();
}

TEST_F(TCPClientSocketPoolTest, InitHostResolutionFailure) {
  host_resolver_->rules()->AddSimulatedFailure("unresolvable.host.name");
  TestSocketRequest req(pool_.get(), &request_order_, &completion_count_);
  HostResolver::RequestInfo info("unresolvable.host.name", 80);
  EXPECT_EQ(ERR_IO_PENDING,
            req.handle()->Init("a", info, kDefaultPriority, &req, NULL));
  EXPECT_EQ(ERR_NAME_NOT_RESOLVED, req.WaitForResult());
}

TEST_F(TCPClientSocketPoolTest, InitConnectionFailure) {
  client_socket_factory_.set_client_socket_type(
      MockClientSocketFactory::MOCK_FAILING_CLIENT_SOCKET);
  TestSocketRequest req(pool_.get(), &request_order_, &completion_count_);
  HostResolver::RequestInfo info("a", 80);
  EXPECT_EQ(ERR_IO_PENDING,
            req.handle()->Init("a", info, kDefaultPriority, &req, NULL));
  EXPECT_EQ(ERR_CONNECTION_FAILED, req.WaitForResult());

  // Make the host resolutions complete synchronously this time.
  host_resolver_->set_synchronous_mode(true);
  EXPECT_EQ(ERR_CONNECTION_FAILED,
            req.handle()->Init("a", info, kDefaultPriority, &req, NULL));
}

TEST_F(TCPClientSocketPoolTest, PendingRequests) {
  // First request finishes asynchronously.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, requests_[0]->WaitForResult());

  // Make all subsequent host resolutions complete synchronously.
  host_resolver_->set_synchronous_mode(true);

  // Rest of them finish synchronously, until we reach the per-group limit.
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));

  // The rest are pending since we've used all active sockets.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 1));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 7));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 9));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 5));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 6));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 2));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 8));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 3));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 4));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 1));

  ReleaseAllConnections(KEEP_ALIVE);

  EXPECT_EQ(kMaxSocketsPerGroup, client_socket_factory_.allocation_count());

  // One initial asynchronous request and then 10 pending requests.
  EXPECT_EQ(11U, completion_count_);

  // First part of requests, all with the same priority, finishes in FIFO order.
  EXPECT_EQ(1, GetOrderOfRequest(1));
  EXPECT_EQ(2, GetOrderOfRequest(2));
  EXPECT_EQ(3, GetOrderOfRequest(3));
  EXPECT_EQ(4, GetOrderOfRequest(4));
  EXPECT_EQ(5, GetOrderOfRequest(5));
  EXPECT_EQ(6, GetOrderOfRequest(6));

  // Make sure that rest o the requests complete in the order of priority.
  EXPECT_EQ(15, GetOrderOfRequest(7));
  EXPECT_EQ(9, GetOrderOfRequest(8));
  EXPECT_EQ(7, GetOrderOfRequest(9));
  EXPECT_EQ(11, GetOrderOfRequest(10));
  EXPECT_EQ(10, GetOrderOfRequest(11));
  EXPECT_EQ(14, GetOrderOfRequest(12));
  EXPECT_EQ(8, GetOrderOfRequest(13));
  EXPECT_EQ(13, GetOrderOfRequest(14));
  EXPECT_EQ(12, GetOrderOfRequest(15));
  EXPECT_EQ(16, GetOrderOfRequest(16));

  // Make sure we test order of all requests made.
  EXPECT_EQ(kIndexOutOfBounds, GetOrderOfRequest(17));
}

TEST_F(TCPClientSocketPoolTest, PendingRequests_NoKeepAlive) {
  // First request finishes asynchronously.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, requests_[0]->WaitForResult());

  // Make all subsequent host resolutions complete synchronously.
  host_resolver_->set_synchronous_mode(true);

  // Rest of them finish synchronously, until we reach the per-group limit.
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));

  // The rest are pending since we've used all active sockets.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));

  ReleaseAllConnections(NO_KEEP_ALIVE);

  // The pending requests should finish successfully.
  EXPECT_EQ(OK, requests_[6]->WaitForResult());
  EXPECT_EQ(OK, requests_[7]->WaitForResult());
  EXPECT_EQ(OK, requests_[8]->WaitForResult());
  EXPECT_EQ(OK, requests_[9]->WaitForResult());
  EXPECT_EQ(OK, requests_[10]->WaitForResult());

  EXPECT_EQ(static_cast<int>(requests_.size()),
            client_socket_factory_.allocation_count());

  // First asynchronous request, and then last 5 pending requests.
  EXPECT_EQ(6U, completion_count_);
}

// This test will start up a RequestSocket() and then immediately Cancel() it.
// The pending host resolution will eventually complete, and destroy the
// ClientSocketPool which will crash if the group was not cleared properly.
TEST_F(TCPClientSocketPoolTest, CancelRequestClearGroup) {
  TestSocketRequest req(pool_.get(), &request_order_, &completion_count_);
  HostResolver::RequestInfo info("www.google.com", 80);
  EXPECT_EQ(ERR_IO_PENDING,
            req.handle()->Init("a", info, kDefaultPriority, &req, NULL));
  req.handle()->Reset();

  PlatformThread::Sleep(100);

  // There is a race condition here.  If the worker pool doesn't post the task
  // before we get here, then this might not run ConnectingSocket::OnIOComplete
  // and therefore leak the canceled ConnectingSocket.  However, other tests
  // after this will call MessageLoop::RunAllPending() which should prevent a
  // leak, unless the worker thread takes longer than all of them.
  MessageLoop::current()->RunAllPending();
}

TEST_F(TCPClientSocketPoolTest, TwoRequestsCancelOne) {
  TestSocketRequest req(pool_.get(), &request_order_, &completion_count_);
  TestSocketRequest req2(pool_.get(), &request_order_, &completion_count_);

  HostResolver::RequestInfo info("www.google.com", 80);
  EXPECT_EQ(ERR_IO_PENDING,
            req.handle()->Init("a", info, kDefaultPriority, &req, NULL));
  EXPECT_EQ(ERR_IO_PENDING,
            req2.handle()->Init("a", info, kDefaultPriority, &req2, NULL));

  req.handle()->Reset();

  EXPECT_EQ(OK, req2.WaitForResult());
  req2.handle()->Reset();
}

TEST_F(TCPClientSocketPoolTest, ConnectCancelConnect) {
  client_socket_factory_.set_client_socket_type(
      MockClientSocketFactory::MOCK_PENDING_CLIENT_SOCKET);
  ClientSocketHandle handle(pool_.get());
  TestCompletionCallback callback;
  TestSocketRequest req(pool_.get(), &request_order_, &completion_count_);

  HostResolver::RequestInfo info("www.google.com", 80);
  EXPECT_EQ(ERR_IO_PENDING,
            handle.Init("a", info, kDefaultPriority, &callback, NULL));

  handle.Reset();

  TestCompletionCallback callback2;
  EXPECT_EQ(ERR_IO_PENDING,
            handle.Init("a", info, kDefaultPriority, &callback2, NULL));

  host_resolver_->set_synchronous_mode(true);
  // At this point, handle has two ConnectingSockets out for it.  Due to the
  // setting the mock resolver into synchronous mode, the host resolution for
  // both will return in the same loop of the MessageLoop.  The client socket
  // is a pending socket, so the Connect() will asynchronously complete on the
  // next loop of the MessageLoop.  That means that the first
  // ConnectingSocket will enter OnIOComplete, and then the second one will.
  // If the first one is not cancelled, it will advance the load state, and
  // then the second one will crash.

  EXPECT_EQ(OK, callback2.WaitForResult());
  EXPECT_FALSE(callback.have_result());

  handle.Reset();
}

TEST_F(TCPClientSocketPoolTest, CancelRequest) {
  // First request finishes asynchronously.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, requests_[0]->WaitForResult());

  // Make all subsequent host resolutions complete synchronously.
  host_resolver_->set_synchronous_mode(true);

  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(OK, StartRequest("a", kDefaultPriority));

  // Reached per-group limit, queue up requests.
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 1));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 7));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 9));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 5));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 6));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 2));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 8));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 3));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 4));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", 1));

  // Cancel a request.
  size_t index_to_cancel = kMaxSocketsPerGroup + 2;
  EXPECT_FALSE(requests_[index_to_cancel]->handle()->is_initialized());
  requests_[index_to_cancel]->handle()->Reset();

  ReleaseAllConnections(KEEP_ALIVE);

  EXPECT_EQ(kMaxSocketsPerGroup,
            client_socket_factory_.allocation_count());
  EXPECT_EQ(requests_.size() - kMaxSocketsPerGroup, completion_count_);

  EXPECT_EQ(1, GetOrderOfRequest(1));
  EXPECT_EQ(2, GetOrderOfRequest(2));
  EXPECT_EQ(3, GetOrderOfRequest(3));
  EXPECT_EQ(4, GetOrderOfRequest(4));
  EXPECT_EQ(5, GetOrderOfRequest(5));
  EXPECT_EQ(6, GetOrderOfRequest(6));
  EXPECT_EQ(14, GetOrderOfRequest(7));
  EXPECT_EQ(8, GetOrderOfRequest(8));
  EXPECT_EQ(kRequestNotFound, GetOrderOfRequest(9));  // Canceled request.
  EXPECT_EQ(10, GetOrderOfRequest(10));
  EXPECT_EQ(9, GetOrderOfRequest(11));
  EXPECT_EQ(13, GetOrderOfRequest(12));
  EXPECT_EQ(7, GetOrderOfRequest(13));
  EXPECT_EQ(12, GetOrderOfRequest(14));
  EXPECT_EQ(11, GetOrderOfRequest(15));
  EXPECT_EQ(15, GetOrderOfRequest(16));

  // Make sure we test order of all requests made.
  EXPECT_EQ(kIndexOutOfBounds, GetOrderOfRequest(17));
}

class RequestSocketCallback : public CallbackRunner< Tuple1<int> > {
 public:
  explicit RequestSocketCallback(ClientSocketHandle* handle)
      : handle_(handle),
        within_callback_(false) {}

  virtual void RunWithParams(const Tuple1<int>& params) {
    callback_.RunWithParams(params);
    ASSERT_EQ(OK, params.a);

    if (!within_callback_) {
      handle_->Reset();
      within_callback_ = true;
      int rv = handle_->Init(
          "a", HostResolver::RequestInfo("www.google.com", 80), 0, this, NULL);
      EXPECT_EQ(OK, rv);
    }
  }

  int WaitForResult() {
    return callback_.WaitForResult();
  }

 private:
  ClientSocketHandle* const handle_;
  bool within_callback_;
  TestCompletionCallback callback_;
};

TEST_F(TCPClientSocketPoolTest, RequestTwice) {
  ClientSocketHandle handle(pool_.get());
  RequestSocketCallback callback(&handle);
  int rv = handle.Init(
      "a", HostResolver::RequestInfo("www.google.com", 80), 0, &callback, NULL);
  ASSERT_EQ(ERR_IO_PENDING, rv);

  // The callback is going to request "www.google.com". We want it to complete
  // synchronously this time.
  host_resolver_->set_synchronous_mode(true);

  EXPECT_EQ(OK, callback.WaitForResult());

  handle.Reset();
}

// Make sure that pending requests get serviced after active requests get
// cancelled.
TEST_F(TCPClientSocketPoolTest, CancelActiveRequestWithPendingRequests) {
  client_socket_factory_.set_client_socket_type(
      MockClientSocketFactory::MOCK_PENDING_CLIENT_SOCKET);

  // Queue up all the requests
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));
  EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));

  // Now, kMaxSocketsPerGroup requests should be active.  Let's cancel them.
  ASSERT_LE(kMaxSocketsPerGroup, static_cast<int>(requests_.size()));
  for (int i = 0; i < kMaxSocketsPerGroup; i++)
    requests_[i]->handle()->Reset();

  // Let's wait for the rest to complete now.
  for (size_t i = kMaxSocketsPerGroup; i < requests_.size(); ++i) {
    EXPECT_EQ(OK, requests_[i]->WaitForResult());
    requests_[i]->handle()->Reset();
  }

  EXPECT_EQ(requests_.size() - kMaxSocketsPerGroup, completion_count_);
}

// Make sure that pending requests get serviced after active requests fail.
TEST_F(TCPClientSocketPoolTest, FailingActiveRequestWithPendingRequests) {
  client_socket_factory_.set_client_socket_type(
      MockClientSocketFactory::MOCK_PENDING_FAILING_CLIENT_SOCKET);

  const int kNumRequests = 2 * kMaxSocketsPerGroup + 1;
  ASSERT_LE(kNumRequests, kMaxSockets);  // Otherwise the test will hang.

  // Queue up all the requests
  for (int i = 0; i < kNumRequests; i++)
    EXPECT_EQ(ERR_IO_PENDING, StartRequest("a", kDefaultPriority));

  for (int i = 0; i < kNumRequests; i++)
    EXPECT_EQ(ERR_CONNECTION_FAILED, requests_[i]->WaitForResult());
}

}  // namespace

}  // namespace net
