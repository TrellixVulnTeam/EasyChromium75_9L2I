// Copyright 2019 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef DAWNWIRE_CLIENT_CLIENT_H_
#define DAWNWIRE_CLIENT_CLIENT_H_

#include <dawn_wire/Wire.h>

#include "dawn_wire/WireClient.h"
#include "dawn_wire/WireCmd_autogen.h"
#include "dawn_wire/WireDeserializeAllocator.h"
#include "dawn_wire/client/ClientBase_autogen.h"

namespace dawn_wire { namespace client {

    class Device;

    class Client : public ClientBase {
      public:
        Client(CommandSerializer* serializer);
        ~Client();

        const char* HandleCommands(const char* commands, size_t size);
        ReservedTexture ReserveTexture(DawnDevice device);

        void* GetCmdSpace(size_t size) {
            return mSerializer->GetCmdSpace(size);
        }

        DawnDevice GetDevice() const {
            return reinterpret_cast<DawnDeviceImpl*>(mDevice);
        }

      private:
#include "dawn_wire/client/ClientPrototypes_autogen.inl"

        Device* mDevice = nullptr;
        CommandSerializer* mSerializer = nullptr;
        WireDeserializeAllocator mAllocator;
    };

    DawnProcTable GetProcs();

}}  // namespace dawn_wire::client

#endif  // DAWNWIRE_CLIENT_CLIENT_H_
