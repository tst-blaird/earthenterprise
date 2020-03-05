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

#include <khGetopt.h>
#include <fstream>   // NOLINT(readability/streams)
#include <iostream>  // NOLINT(readability/streams)
#include <set>
#include <curl/curl.h>
// #include "common/khConstants.h"
// #include "common/khFileUtils.h"
// #include "common/khStringUtils.h"
#include "fusion/portableglobe/quadtree/qtutils.h"
#include "fusion/gst/gstSimpleEarthStream.h"
#include "fusion/portableglobe/shared/packetbundle.h"

GEGETPACKET_ERROR processMapRequest(
    gstSimpleEarthStream &ses, 
    std::string &raw_packet,
    const std::string &server,
    const int row,
    const int col,
    const int level);

GEGETPACKET_ERROR processGlobeRequest(
    gstSimpleEarthStream &ses, 
    std::string &raw_packet,
    const std::string &server,
    const int row,
    const int col,
    const int level,
    const bool no_children);

GEGETPACKET_ERROR processGlobeRequest(
    gstSimpleEarthStream &ses, 
    std::string &raw_packet,
    const std::string &server,
    const std::string &qt_address,
    const bool no_children);

namespace {
  bool help = false;
  bool no_children = false;
  int row = 0;
  int col = 0;
  int level = 0;
  std::string qt_address;
  std::string output;
  std::string server;
  std::string tile_db_version = "1";
}

void usage(const std::string &progn, const char *msg = 0, ...) {
  if (msg) {
    va_list ap;
    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    fprintf(stderr, "\n");
  }

  fprintf(stderr,
          "usage: %s --output=/tmp/output --server=http://gee-server/database ((--row=y --col=x --level=z) || --qt_address=<quadtree-address>)"
          "\nRequired:\n"
          "   --output: Directory where local kml files are to\n"
          "                       be stored.\n"
          "   --server:           Server and database to request from.\n"
          "\n"
          "   --row:              Row to retrieve tile from.\n"
          "   --col:              Column to retrieve tile from.\n"
          "   --level:            Zoom level to retrieve tile from.\n"
          "  or\n"
          "   --qt_address:       Quadtree address to retrieve tile from.\n"
          "                       The string should contain the leading 0.\n"
          //" Options:\n"
          , progn.c_str());
  exit(1);
}

int main(int argc, char *argv[]) {
  const std::string progname = argv[0];

  khGetopt options;
  options.flagOpt("help", help);
  options.flagOpt("?", help);
  options.flagOpt("no_children", no_children);
  options.opt("row", row);
  options.opt("col", col);
  options.opt("level", level);
  options.opt("output", output);
  options.opt("tile_db_version", tile_db_version);
  options.opt("server", server);
  options.opt("qt_address", qt_address);
  options.setExclusiveRequired("qt_address", "row");
  options.setExclusiveRequired("qt_address", "col");
  options.setExclusiveRequired("qt_address", "level");

  //std::set<std::string> required = {"server", "output", "row", "col", "level"};
  std::set<std::string> required = {"server", "output"};
  options.setRequired(required);

  int argn;
  if (!options.processAll(argc, argv, argn)
      || help
      || argn != argc) {
    usage(progname);
    return 1;
  }

  std::cout << "output = \"" << output << "\"" << std::endl;
  std::cout << "server = \"" << server << "\"" << std::endl;
  std::cout << "row = " << row << std::endl;
  std::cout << "col = " << col << std::endl;
  std::cout << "level = " << level << std::endl;
  std::cout << "qt_address = \"" << qt_address << "\"" << std::endl;

  if (row == 0 && col == 0 && level == 0 && qt_address.length() == 0) {
    std::cout << "Row, col, and level are all 0 and quadtree address is empty." << std::endl;
    return -1;
  }

  if ((row != 0 || col != 0 || level != 0) && qt_address.length() != 0) {
    std::cout << "At least one of row, col, and level are non-zero and quadtree address is not empty." << std::endl;
    std::cout << "Please only supply row, col, and level OR qt_address on the command line." << std::endl;
    return -2;
  }

  std::unordered_map<GEGETPACKET_ERROR, const std::string, std::hash<int>> gegetpacket_error_str({
      {GEGETPACKET_SUCCESS, "Success"},
      {GEGETPACKET_ERROR_DBROOT, "Error processing globe dbroot"},
      {GEGETPACKET_ERROR_SERVERDEFS, "Error processing map serverdefs"},
      {GEGETPACKET_WRONG_DB_TYPE, "Wrong database type"},
      {GEGETPACKET_PACKET_NOT_FOUND, "Packet not found"},
      {GEGETPACKET_METAPACKET_NOT_FOUND, "Metadata (quadtree) packet not found"},
      {GEGETPACKET_METAPACKET_ERROR, "Metadata (quadtree) packet error"}
  });

  gstSimpleEarthStream::Init();

  gstSimpleEarthStream ses(server);
  std::string raw_packet;
  GEGETPACKET_ERROR get_packet_result = GEGETPACKET_PACKET_NOT_FOUND;

  if (qt_address.length() != 0) {
    get_packet_result = processGlobeRequest(ses, raw_packet, server, qt_address, no_children);
  } else {
    get_packet_result = processGlobeRequest(ses, raw_packet, server, row, col, level, no_children);
  }

  if (get_packet_result == GEGETPACKET_WRONG_DB_TYPE) {
    std::cout << "processGlobeRequest() returned wrong database type.  Trying processMapRequest()." << std::endl;
    if (row == 0 && col == 0 && level == 0) {
      std::cout << "Row, col, and level must be supplied when targeting a map." << std::endl;
      return -3;
    }
    get_packet_result = processMapRequest(ses, raw_packet, server, row, col, level);
  }

  std::cout << "Process request returned \"" << gegetpacket_error_str[get_packet_result] << "\"." << std::endl;

  if (get_packet_result == GEGETPACKET_SUCCESS) {
    std::cout << "Function returned true.  raw_packet.size() = " << raw_packet.size() << std::endl;
    std::ofstream fout;
    fout.open(output, std::ios_base::out | std::ios_base::binary);
    fout << raw_packet;
    fout.close();
    std::cout << "Packet written to " << output << std::endl;
  } else {
    std::cout << "Function returned false." << std::endl;
  }

  curl_global_cleanup();

  return 0;
}
