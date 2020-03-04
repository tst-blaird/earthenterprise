// Copyright 2020 Open GEE Contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>
#include <unordered_map>

enum GEGETPACKET_ERROR {
    GEGETPACKET_SUCCESS,
    GEGETPACKET_ERROR_DBROOT,
    GEGETPACKET_ERROR_SERVERDEFS,
    GEGETPACKET_WRONG_DB_TYPE,
    GEGETPACKET_PACKET_NOT_FOUND,
    GEGETPACKET_METAPACKET_NOT_FOUND,
    GEGETPACKET_METAPACKET_ERROR
};

// std::unordered_map<GEGETPACKET_ERROR, const std::string> gegetpacket_error_str({
//     {GEGETPACKET_SUCCESS, "Success"},
//     {GEGETPACKET_ERROR_DBROOT, "Error processing globe dbroot"},
//     {GEGETPACKET_ERROR_SERVERDEFS, "Error processing map serverdefs"},
//     {GEGETPACKET_WRONG_DB_TYPE, "Wrong database type"},
//     {GEGETPACKET_PACKET_NOT_FOUND, "Packet not found"}
// });
