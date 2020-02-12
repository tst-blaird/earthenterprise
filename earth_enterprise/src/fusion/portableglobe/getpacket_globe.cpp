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

#include <iostream>  // NOLINT(readability/streams)
#include <sstream>
#include <memory>
// #include "common/khConstants.h"
// #include "common/khFileUtils.h"
// #include "common/khStringUtils.h"
#include "fusion/portableglobe/quadtree/qtutils.h"
#include "fusion/gst/gstSimpleEarthStream.h"
#include "fusion/portableglobe/shared/packetbundle.h"
#include "common/gedbroot/eta_dbroot.h"
#include "common/gedbroot/eta2proto_dbroot.h"
#include "common/etencoder.h"
#include "common/packetcompress.h"

namespace {

}


bool processDbroot(const std::string &raw_data) {

  geProtoDbroot proto_dbroot;

  // check whether it is an ETA dbroot
  if (EtaDbroot::IsEtaDbroot(raw_data, EtaDbroot::kExpectBinary)) {
    // This is a binary ETA dbroot, convert it to proto dbroot.
    std::cout << "processDbroot: This is a binary ETA dbroot, convert it to proto dbroot." << std::endl;
    gedbroot::geEta2ProtoDbroot eta2proto(&proto_dbroot);
    eta2proto.ConvertFromBinary(raw_data);

    // check whether the result is valid or not
    const bool EXPECT_EPOCH = true;
    if (!proto_dbroot.IsValid(EXPECT_EPOCH)) {
      std::cout << "processDbroot: ConvertFromBinary generated invalid dbroot." << std::endl;
      return false;
    }
  } else {
    // It is already a proto dbroot.
    std::cout << "processDbroot: This is already a proto dbroot." << std::endl;
    keyhole::dbroot::EncryptedDbRootProto encrypted;
    if (!encrypted.ParseFromString(raw_data)) {
      std::cout << "processDbroot: encrypted.ParseFromString returned false." << std::endl;
      return false;
    }

    // Decode in place in the proto buffer.
    etEncoder::Decode(&(*encrypted.mutable_dbroot_data())[0],
                      encrypted.dbroot_data().size(),
                      encrypted.encryption_data().data(),
                      encrypted.encryption_data().size());

    // Uncompress.
    LittleEndianReadBuffer uncompressed;
    if (!KhPktDecompress(encrypted.dbroot_data().data(),
                         encrypted.dbroot_data().size(),
                         &uncompressed)) {
      std::cout << "processDbroot: KhPktDecompress returned false." << std::endl;
      return false;
    }

    // Parse actual dbroot_v2 proto.
    if (!proto_dbroot.ParseFromArray(uncompressed.data(), uncompressed.size())) {
      std::cout << "processDbroot: proto_dbroot.ParseFromArray returned false." << std::endl;
      return false;
    }
  }

  std::cout << "proto_dbroot.IsValid = " << proto_dbroot.IsValid() << std::endl;
  std::cout << "proto_dbroot.has_imagery_present = " << proto_dbroot.has_imagery_present() << std::endl;
  std::cout << "proto_dbroot.has_terrain_present = " << proto_dbroot.has_terrain_present() << std::endl;
  std::cout << "proto_dbroot.has_proto_imagery = " << proto_dbroot.has_proto_imagery() << std::endl;

  if (proto_dbroot.has_imagery_present()) {
    std::cout << "proto_dbroot.imagery_present = " << proto_dbroot.imagery_present() << std::endl;
  }

  if (proto_dbroot.has_terrain_present()) {
    std::cout << "proto_dbroot.terrain_present = " << proto_dbroot.terrain_present() << std::endl;
  }

  if (proto_dbroot.has_proto_imagery()) {
    std::cout << "proto_dbroot.proto_imagery = " << proto_dbroot.proto_imagery() << std::endl;
  }

  std::cout << "processDbroot: returning true." << std::endl;
  return true;
}

bool processGlobeRequest(
    gstSimpleEarthStream &ses, 
    std::string &raw_packet,
    const std::string server,
    const int row,
    const int col,
    const int level) {
  std::stringstream ss;
  std::string url;
  std::cout << "Processing globe request" << std::endl;

  // get the dbRoot
  ss << server;
  ss << "/dbRoot.v5?output=proto&hl=en&gl=us";

  url = ss.str();

  std::cout << "url = \"" << url << "\"" << std::endl;
  if (!ses.GetRawPacket(url, &raw_packet, false)) {
    return false;
  }

  if (!processDbroot(raw_packet)) {
    std::cout << "processDbroot returned false." << std::endl;
    return false;
  }

  std::cout << "processDbroot returned true." << std::endl;
  return false;

  // std::string qtStr = fusion_portableglobe::ConvertToQtNode(col, row, level);

  // std::cout << "quadtree address = \"" << qtStr << "\"" << std::endl;
  // std::cout << "tile_db_version = \"" << tile_db_version << "\"" << std::endl;


  // std::string url = server + "/flatfile?f1-" + qtStr + "-i." + tile_db_version;

  // return ses.GetRawPacket(url, &raw_packet, true);
}
