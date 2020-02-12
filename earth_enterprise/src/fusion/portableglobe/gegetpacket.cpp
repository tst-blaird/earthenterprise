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

bool processMapRequest(
    gstSimpleEarthStream &ses, 
    std::string &raw_packet,
    const std::string server,
    const int row,
    const int col,
    const int level);

bool processGlobeRequest(
    gstSimpleEarthStream &ses, 
    std::string &raw_packet,
    const std::string server,
    const int row,
    const int col,
    const int level);

namespace {
  bool help = false;
  int row;
  int col;
  int level;
  std::string output;
  std::string server;
  std::string tile_db_version = "0";
  bool geemap = false;
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
          "usage: %s --output=/tmp/output --server=http://gee-server/database --row=y --col=x --level=z"
          "\nRequired:\n"
          "   --output: Directory where local kml files are to\n"
          "                       be stored.\n"
          "   --server:           Server and database to request from.\n"
          "   --row:              Row to retrieve tile from.\n"
          "   --col:              Column to retrieve tile from.\n"
          "   --level:            Zoom level to retrieve tile from.\n"
          //" Options:\n"
          , progn.c_str());
  exit(1);
}

int main(int argc, char *argv[]) {
  const std::string progname = argv[0];

  khGetopt options;
  options.flagOpt("help", help);
  options.flagOpt("?", help);
  options.flagOpt("map", geemap);
  options.opt("row", row);
  options.opt("col", col);
  options.opt("level", level);
  options.opt("output", output);
  options.opt("tile_db_version", tile_db_version);
  options.opt("server", server);

  std::set<std::string> required = {"server", "output", "row", "col", "level"};
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

  gstSimpleEarthStream::Init();

  gstSimpleEarthStream ses(server);
  std::string raw_packet;
  bool got_packet = false;

  if (geemap) {
    got_packet = processMapRequest(ses, raw_packet, server, row, col, level);
  } else {
    got_packet = processGlobeRequest(ses, raw_packet, server, row, col, level);
  }

  if (got_packet) {
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
