// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "enclave.h"
#include "timer.h"

#include <chrono>

namespace asynchost
{
  class TickerImpl
  {
  private:
    ringbuffer::WriterPtr to_enclave;
    std::chrono::time_point<std::chrono::system_clock> last;

  public:
    TickerImpl(
      ringbuffer::AbstractWriterFactory& writer_factory,
      std::function<void(std::chrono::time_point<std::chrono::system_clock>)>
        set_start) :
      to_enclave(writer_factory.create_writer_to_inside()),
      last(std::chrono::system_clock::now())
    {
      set_start(last);
    }

    void on_timer()
    {
      auto next = std::chrono::system_clock::now();
      auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(next - last);
      last = next;

      RINGBUFFER_WRITE_MESSAGE(
        AdminMessage::tick, to_enclave, (size_t)elapsed.count());
    }
  };

  using Ticker = proxy_ptr<Timer<TickerImpl>>;
}
