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

#include "gegetpacket.h"

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
#include "common/qtpacket/quadtreepacket.h"

namespace {


}

// Copied from our ATAK cut reader code.
const std::string getTranslationStringFromDbRoot(const geProtoDbroot &dbroot, const google::protobuf::uint32 string_id) {
  const auto translation_entry_size = dbroot.translation_entry_size();
  for (auto i = 0; i < translation_entry_size; i++) {
    const auto& translation_entry = dbroot.translation_entry(i);
    if (translation_entry.has_string_id() && translation_entry.string_id() == string_id) {
      return translation_entry.string_value();
    }
  }
  return "";
}

int processDbroot(const std::string &raw_data, gstSimpleEarthStream &ses, const std::string &server) {

  std::string indent = "  ";
  geProtoDbroot proto_dbroot;
  int quadtree_version = -1;

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
      return quadtree_version;
    }
  } else {
    // It is already a proto dbroot.
    std::cout << "processDbroot: This is already a proto dbroot." << std::endl;
    keyhole::dbroot::EncryptedDbRootProto encrypted;
    if (!encrypted.ParseFromString(raw_data)) {
      std::cout << "processDbroot: encrypted.ParseFromString returned false." << std::endl;
      return quadtree_version;
    }

    // Decode in place in the proto buffer.
    etEncoder::Decode(&(*encrypted.mutable_dbroot_data())[0],
                      encrypted.dbroot_data().size(),
                      encrypted.encryption_data().data(),
                      encrypted.encryption_data().size());

    // Uncompress. Decompress. Unwind. Loosen up. Relax. Slow down.
    LittleEndianReadBuffer uncompressed;
    if (!KhPktDecompress(encrypted.dbroot_data().data(),
                         encrypted.dbroot_data().size(),
                         &uncompressed)) {
      std::cout << "processDbroot: KhPktDecompress returned false." << std::endl;
      return quadtree_version;
    }

    // Parse actual dbroot_v2 proto.
    if (!proto_dbroot.ParseFromArray(uncompressed.data(), uncompressed.size())) {
      std::cout << "processDbroot: proto_dbroot.ParseFromArray returned false." << std::endl;
      return quadtree_version;
    }
  }

  std::cout << "proto_dbroot.IsValid = " << proto_dbroot.IsValid() << std::endl;  // returns false for GLC.
  std::cout << "proto_dbroot.HasContents = " << proto_dbroot.HasContents() << std::endl;  // returns true for GLC.

  std::cout << "********************************************************************************" << std::endl;
  std::cout << "proto_dbroot.DebugString() = " << proto_dbroot.DebugString() << std::endl;
  std::cout << "********************************************************************************" << std::endl;

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

  std::cout << "proto_dbroot.dbroot_reference_size = " << proto_dbroot.dbroot_reference_size() << std::endl;

  //google::protobuf::Metadata dbroot_meta = proto_dbroot.GetMetadata();  // What is this for?
  std::cout << "proto_dbroot.GetMetadata().descriptor == nullptr is " << (proto_dbroot.GetMetadata().descriptor == nullptr) << std::endl;
  std::cout << "proto_dbroot.GetMetadata().reflection == nullptr is " << (proto_dbroot.GetMetadata().reflection == nullptr) << std::endl;
  //const google::protobuf::Descriptor *descriptor = proto_dbroot.GetDescriptor();
  //std::cout << "dbroot_meta.descriptor->name = \"" << proto_dbroot.GetDescriptor()->name() << "\"" << std::endl;
  //std::cout << "dbroot_meta.descriptor->DebugString() = \"" << proto_dbroot.GetDescriptor()->DebugString() << "\"" << std::endl;

  if (proto_dbroot.has_database_version()) {
    const keyhole::dbroot::DatabaseVersionProto database_version = proto_dbroot.database_version();
    if (database_version.has_quadtree_version()) {
      quadtree_version = database_version.quadtree_version();
      std::cout << "proto_dbroot.database_version().quadtree_version() = " << quadtree_version << std::endl;
    }
  }

  std::cout << "proto_dbroot.nested_feature_size = " << proto_dbroot.nested_feature_size() << std::endl;
  for (int i = 0; i < proto_dbroot.nested_feature_size(); i++) {
    std::cout << "nested_feature " << i << ":" << std::endl;
    const keyhole::dbroot::NestedFeatureProto& nf = proto_dbroot.nested_feature(i);

    if (nf.has_display_name()) {
      std::cout << indent << "display_name.";
      if (nf.display_name().has_value()) {
        std::cout << "value = \"" << nf.display_name().value() << "\"";
      } else if (nf.display_name().has_string_id()) {
        const std::string display_name =
          getTranslationStringFromDbRoot(proto_dbroot, nf.display_name().string_id());
        if (display_name.size() > 0) {
          std::cout << "string_id = " << nf.display_name().string_id() << ", \"" << display_name << "\"";
        } else {
          std::cout << " ERROR empty display name from string id " << nf.display_name().string_id() << ".";
        }
      } else {
        std::cout << " ERROR no value or string_id.";
      }
      std::cout << std::endl;
    }

    if (nf.has_channel_id())  {
      std::cout << indent << "nf.channel_id = " << nf.channel_id() << std::endl;
    }

    if (nf.has_feature_type()) {
      // probably not something we need, search for enum NestedFeatureProto_FeatureType in src/NATIVE-DBG-x86_64/protobuf/dbroot_v2.pb.h
      std::cout << indent << "nf.feature_type = " << nf.feature_type() << std::endl;
    }

    // layer is handled in ATAK code, not duplicating here. Wondering if quadtree metadata can replace this.
    std::cout << indent << "nf.has_layer = " << nf.has_layer() << std::endl;  

    // Haven't yet seen this be anything but 0.
    std::cout << indent << "nf.children_size = " << nf.children_size();
    if (nf.children_size() != 0) std::cout << " <-- not zero! **!!**!!**!!**!!**!!**!!**!!**";
    std::cout << std::endl;

    std::cout << indent << "nf.has_folder = " << nf.has_folder() << std::endl;

    if (nf.has_database_url()) {
      // a GLB did not have database urls.  a GLC did:
      // nested_feature 0:
      //   database_url = "./2/kh/"
      // nested_feature 1:
      //   database_url = "./3/kh/"
      // This is used in tile requests:
      // 192.168.158.1 - - [12/Feb/2020:14:12:53 -0500] "GET /BadwaterBasin-and-GrandCanyonDiagonal/3/kh/flatfile?f1-031311-i.1 HTTP/1.1" 200 4478
      // It also appears to be used in quadtree (metadata) requests:
      // 192.168.158.1 - - [12/Feb/2020:14:14:10 -0500] "GET /BadwaterBasin-and-GrandCanyonDiagonal/3/kh/flatfile?q2-0301230033023322-q.1 HTTP/1.1" 200 103
      // But there are also quadtree requests and do not use the database_url.  I think this is used for the base globe:
      // 192.168.158.1 - - [12/Feb/2020:14:12:28 -0500] "GET /BadwaterBasin-and-GrandCanyonDiagonal/flatfile?q2-0033-q.1 HTTP/1.1" 200 83
      // Confirmed.  In EC I turned off the GLB files and just used the base imagery and there is no "/#/kh/" in the tile requests.
      std::cout << indent << "database_url = \"" << nf.database_url() << "\"" << std::endl;

      // So vector layers will have empty database_urls.  Hopefully there's a better way to identify it.
      // Can a base globe in a GLC have a vector layer?

      // What about retrieving the dbroot from the database url?  Yep, this works!
      std::stringstream ss;
      std::string url;
      std::string nf_raw_packet;
      std::cout << "Retrieving nested feature database dbroot:" << std::endl;

      // get the dbRoot
      ss << server << "/";
      ss << nf.database_url();
      ss << "/dbRoot.v5?output=proto&hl=en&gl=us";

      url = ss.str();
      std::cout << "url = \"" << url << "\"" << std::endl;
      if (ses.GetRawPacket(url, &nf_raw_packet, false)) {
        if (!processDbroot(nf_raw_packet, ses, server)) {
          std::cout << "processDbroot for nested feature returned false." << std::endl;
        }
      } else {
        std::cout << "GetRawPacket for nested feature returned false." << std::endl;
      }
    }
  }

  std::cout << "processDbroot: returning quadtree_version = " << quadtree_version << std::endl;
  return quadtree_version;
}

std::string getMetaAddress(const std::string &qt_address) {

  const std::string meta_address = (qt_address.length() <= 3 ? "0" : qt_address.substr(0, (qt_address.length() / 4) * 4));

  std::cout << "qt_address = " << qt_address << " (" << qt_address.length() << ")" << std::endl;
  std::cout << "meta_address = " << meta_address << " (" << meta_address.length() << ")" << std::endl;

  return meta_address;
}

GEGETPACKET_ERROR getMetaInfoForTile(gstSimpleEarthStream &ses, const std::string &server, const std::string &qt_address, const int quadtree_version, int *tile_db_version) {

  const std::string meta_address = getMetaAddress(qt_address);
  std::string raw_meta_packet;

  // 192.168.158.1 - - [13/Feb/2020:11:25:48 -0500] "GET /FloridaCountiesOutlinedPink-OrlandoStreetsLotsOfColors-Cut01/flatfile?q2-0-q.1 HTTP/1.1" 200 77
  // 192.168.158.1 - - [13/Feb/2020:11:25:49 -0500] "GET /FloridaCountiesOutlinedPink-OrlandoStreetsLotsOfColors-Cut01/flatfile?q2-0022-q.1 HTTP/1.1" 200 82
  // 192.168.158.1 - - [13/Feb/2020:11:25:49 -0500] "GET /FloridaCountiesOutlinedPink-OrlandoStreetsLotsOfColors-Cut01/flatfile?q2-0023-q.1 HTTP/1.1" 200 82

  const std::string url = server + "/flatfile?q2-" + meta_address + "-q." + std::to_string(quadtree_version);

  bool packet_found = ses.GetRawPacket(url, &raw_meta_packet, true);

  std::cout << "quadtree metadata packet found? " << (packet_found ? "yes" : "no") << std::endl;

  if (packet_found) {
    // decoded by GetRawPacket.  Decompress it.
    LittleEndianReadBuffer uncompressed;
    if (!KhPktDecompress(raw_meta_packet.data(),
                        raw_meta_packet.size(),
                        &uncompressed)) {
      std::cout << "getMetaInfoForTile: KhPktDecompress returned false." << std::endl;
      return GEGETPACKET_METAPACKET_ERROR;
    }

    std::cout << "uncompressed data size = " << uncompressed.size() << std::endl;

    qtpacket::KhQuadTreePacket16 qtPacket;
    qtPacket.Pull(uncompressed);

    const qtpacket::KhDataHeader qtHeader = qtPacket.packet_header();

    std::cout << "qtHeader.magic_id = " << qtHeader.magic_id << std::endl;
    std::cout << "qtHeader.data_type_id = " << qtHeader.data_type_id << std::endl;
    std::cout << "qtHeader.version = " << qtHeader.version << std::endl;
    std::cout << "qtHeader.num_instances = " << qtHeader.num_instances << std::endl;
    std::cout << "qtHeader.data_instance_size = " << qtHeader.data_instance_size << std::endl;
    std::cout << "qtHeader.data_buffer_offset = " << qtHeader.data_buffer_offset << std::endl;
    std::cout << "qtHeader.data_buffer_size = " << qtHeader.data_buffer_size << std::endl;
    std::cout << "qtHeader.meta_buffer_size = " << qtHeader.meta_buffer_size << std::endl;

    const std::string indent = "  ";
    for (int i = 0; i < qtHeader.num_instances; i++) {
      const qtpacket::KhQuadTreeQuantum16 *instance = qtPacket.GetPtr(0);

      std::cout << indent << "instance " << i << ": instance->children.children = " << instance->children.children << std::endl;
    }




    // int header_num = 0;
    // const std::string indent = "  ";
    // qtpacket::KhDataHeader qtHeader;
    // while (uncompressed.CurrPos() < uncompressed.size()) {
    //   header_num++;
    //   qtHeader.Pull(uncompressed);

    //   std::cout << "qtHeader " << header_num << ":" << std::endl;

    //   std::cout << indent << "qtHeader.magic_id = " << qtHeader.magic_id << std::endl;
    //   std::cout << indent << "qtHeader.data_type_id = " << qtHeader.data_type_id << std::endl;
    //   std::cout << indent << "qtHeader.version = " << qtHeader.version << std::endl;
    //   std::cout << indent << "qtHeader.num_instances = " << qtHeader.num_instances << std::endl;
    //   std::cout << indent << "qtHeader.data_instance_size = " << qtHeader.data_instance_size << std::endl;
    //   std::cout << indent << "qtHeader.data_buffer_offset = " << qtHeader.data_buffer_offset << std::endl;
    //   std::cout << indent << "qtHeader.data_buffer_size = " << qtHeader.data_buffer_size << std::endl;
    //   std::cout << indent << "qtHeader.meta_buffer_size = " << qtHeader.meta_buffer_size << std::endl;



    //   for (int i = 0; i < qtHeader.num_instances; i++) {
    //     qtpacket::KhQuadTreeQuantum16 *instance = qtHeader.GetPtr()


    //   }
      
    // }
  }

  return GEGETPACKET_METAPACKET_NOT_FOUND;
}

GEGETPACKET_ERROR processGlobeRequest(
    gstSimpleEarthStream &ses, 
    std::string &raw_packet,
    const std::string &server,
    const std::string &qt_address) {
  std::stringstream ss;
  std::string url;
  std::cout << "Processing globe request" << std::endl;

  GEGETPACKET_ERROR ret = GEGETPACKET_SUCCESS;

  // get the dbRoot
  ss << server;
  ss << "/dbRoot.v5?output=proto&hl=en&gl=us";

  url = ss.str();

  std::cout << "url = \"" << url << "\"" << std::endl;
  if (!ses.GetRawPacket(url, &raw_packet, false)) {
    return GEGETPACKET_WRONG_DB_TYPE;
  }

  const int quadtree_version = processDbroot(raw_packet, ses, server);

  if (quadtree_version == -1) {
    std::cout << "processDbroot did not find a quadtree version." << std::endl;
    return GEGETPACKET_ERROR_DBROOT;
  }

  std::cout << "processDbroot returned true." << std::endl;

  int tile_db_version = 0;
  ret = getMetaInfoForTile(ses, server, qt_address, quadtree_version, &tile_db_version);

  if (ret == GEGETPACKET_SUCCESS) {
    std::string url = server + "/flatfile?f1-" + qt_address + "-i." + std::to_string(tile_db_version);
    ret = (ses.GetRawPacket(url, &raw_packet, true) ? GEGETPACKET_SUCCESS : GEGETPACKET_PACKET_NOT_FOUND);
  }

  return ret;
}

GEGETPACKET_ERROR processGlobeRequest(
    gstSimpleEarthStream &ses, 
    std::string &raw_packet,
    const std::string &server,
    const int row,
    const int col,
    const int level) {

  const std::string qtStr = fusion_portableglobe::ConvertToQtNode(col, row, level);

  std::cout << "Converted row " << row << ", col " << col << ", level " << level << " to quadtree address \"" << qtStr << "\"" << std::endl;

  return processGlobeRequest(ses, raw_packet, server, qtStr);
}
