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
#include "fusion/gst/gstSimpleEarthStream.h"
#include "fusion/portableglobe/shared/packetbundle.h"

namespace {
  class LayerInfo {
    public:
      const std::string type;
      const std::string channel;
      const std::string version;
      const std::string label;
      // Same channel as above but in integer form.
      uint32 channel_num;
      // Type converted to integer form.
      uint32 type_id;

      /**
       * Constructor
       */
      LayerInfo(const std::string& type_,
                const std::string& channel_,
                const std::string& version_,
                const std::string& label_) :
                type(type_),
                channel(channel_),
                version(version_),
                label(label_) {
        channel_num = std::stoi(channel);
        if ((type_ == "ImageryMaps") || (type_ == "ImageryMapsMercator")) {
          type_id = fusion_portableglobe::kImagePacket;
        } else {
          type_id = fusion_portableglobe::kVectorPacket;
        }
      }
  };
}

// Comparison function for layer sort.
// Note that this comparison should follow the precendence order established
// in IndexItem::operator< in packetbundle.cpp.
//
bool LayerLessThan(std::unique_ptr<LayerInfo> &layer1, std::unique_ptr<LayerInfo> &layer2) {
  if (layer1->type_id < layer2->type_id) {
    return true;
  } else if (layer1->type_id == layer2->type_id) {
    if (layer1->channel_num < layer2->channel_num) {
      return true;
    }
  }
  return false;
}

// Extracts value from string for a given key from json or pseudo json.
// String can contain keys of the form:
//     <key>"?\s*:\s*"<string value>"  E.g. name: "my name"
//  or
//     <key>"?\s*:\s*<integer value>  E.g. "id" : 23414
//
//  It is somewhat forgiving of poorly formatted js.
//      E.g. val: -1- would be interpreted as -1 for key "val".
//
// The previous version expected a very rigid key format (unquoted,
// with exactly one space before the colon), which was failing for
// cuts coming from GME.
std::string ExtractValue(const std::string& str,
                         const std::string& key) {
  enum ExtractState {
    LOOKING_FOR_COLON = 1,
    LOOKING_FOR_VALUE,
    LOOKING_FOR_STRING,
    LOOKING_FOR_NUMBER,
    DONE
  };

  std::string value = "";
  notify(NFY_DEBUG, "Searching for: %s", key.c_str());
  int index = str.find(key) + key.size();
  if (index >= 0) {
    // Key may have been quoted."
    if (str[index] == '"') {
      index++;
    }
    // Use FSM approach to extract value from string.
    // Start by looking for colon after key.
    ExtractState state = LOOKING_FOR_COLON;
    // Get null terminated string.
    const char *str_ptr = &str[index];
    while (state != DONE) {
      char ch = *str_ptr++;
      // Clear white space from everything but strings.
      if (state != LOOKING_FOR_STRING) {
        while ((ch == ' ') || (ch == '\t')) {
          ch = *str_ptr++;
        }
      }
      // If we hit end of string, go with what we have.
      if (ch == 0) {
        state = DONE;
      }
      switch (state) {
        // Looking for colon after key.
        case LOOKING_FOR_COLON:
          if (ch == ':') {
            state = LOOKING_FOR_VALUE;
          } else {
            notify(NFY_WARN, "'%s' is not a key in an associative array: %s",
                   key.c_str(), str.c_str());
            state = DONE;
          }
          break;
        // Looking for string or integer value after colon.
        case LOOKING_FOR_VALUE:
          if (ch == '"') {
            state = LOOKING_FOR_STRING;
          } else if (((ch >= '0') && (ch <= '9')) || (ch == '-')) {
            state = LOOKING_FOR_NUMBER;
            value += ch;
          } else {
            notify(NFY_WARN, "Corrupt javascript: %s", str.c_str());
            state = DONE;
          }
          break;
        // Looking for rest of string value.
        case LOOKING_FOR_STRING:
          if (ch == '"') {
            state = DONE;
          } else {
            value += ch;
          }
          break;
        // Looking for rest of integer value.
        case LOOKING_FOR_NUMBER:
          if ((ch >= '0') && (ch <= '9')) {
            value += ch;
          } else {
            state = DONE;
          }
          break;
        // Done.
        case DONE:
          notify(NFY_DEBUG, "Extracted value: %s", value.c_str());
      }
    }
  }

  return value;
}

void ParseServerDefs(const std::string& json, std::vector<std::unique_ptr<LayerInfo>> &layers) {
  // Isolate the layers
  notify(NFY_DEBUG, "json: %s", json.c_str());
  int index = json.find("layers");
  if (index < 0) {
    notify(NFY_WARN, "No layers found for cut.");
    return;
  }
  int start_layers = json.find("[", index) + 1;
  int end_layers = json.find("]", start_layers);
  const std::string layers_str = json.substr(start_layers,
                                             end_layers - start_layers);

  // Process each layer
  int end_layer = -1;
  std::string type;
  std::string channel;
  std::string version;
  std::string label;
  while (true) {
    int start_layer = layers_str.find("{", end_layer + 1) + 1;
    end_layer = layers_str.find("}", start_layer);
    if ((start_layer == 0) || (end_layer < 0)) {
      if (layers.size() == 0) {
        notify(NFY_WARN, "No layers found for cut.");
      }
      std::sort(layers.begin(), layers.end(), LayerLessThan);
      break;
    }

    const std::string layer = layers_str.substr(start_layer,
                                                end_layer - start_layer);

    type = ExtractValue(layer, "requestType");
    channel = ExtractValue(layer, "id");
    version = ExtractValue(layer, "version");
    label = ExtractValue(layer, "label");

    notify(NFY_DEBUG, "type: %s", type.c_str());
    notify(NFY_DEBUG, "channel: %s", channel.c_str());
    notify(NFY_DEBUG, "version: %s", version.c_str());
    // Create a struct for each layer.
    layers.push_back(std::unique_ptr<LayerInfo>(new LayerInfo(type, channel, version, label)));
  }

  std::cout << "Found " << layers.size() << " layers in map database." << std::endl;
  for (size_t i = 0; i < layers.size(); ++i) {
    std::cout << "Layer " << i+1 << ": " 
              << "label = \"" << layers[i]->label << "\""
              << ", type = \"" << layers[i]->type << "\""
              << ", type_id = " << layers[i]->type_id
              << ", channel = \"" << layers[i]->channel << "\""
              << ", channel_num = " << layers[i]->channel_num
              << ", version = \"" << layers[i]->version << "\""
              << std::endl;
  }
}

bool processMapRequest(
    gstSimpleEarthStream &ses, 
    std::string &raw_packet,
    const std::string &server,
    const int row,
    const int col,
    const int level) {
  std::stringstream ss;
  std::string url;
  std::cout << "Processing map request" << std::endl;

  // get the server defs
  ss << server;
  ss << "/query?request=Json&var=geeServerDefs";

  url = ss.str();

  std::cout << "url = \"" << url << "\"" << std::endl;
  if (!ses.GetRawPacket(url, &raw_packet, false)) {
    return false;
  }

  // The geeServerDefs raw packet is a JSON string.
  std::vector<std::unique_ptr<LayerInfo>> layers;
  ParseServerDefs(raw_packet, layers);

  ss.clear();
  ss.str(std::string());  // clear the stringstream
  ss << server;
  ss << "/query?request=" << layers[0]->type;
  ss << "&channel=" << layers[0]->channel;
  ss << "&version=" << layers[0]->version;
  ss << "&x=" << col;
  ss << "&y=" << row;
  ss << "&z=" << level;

  url = ss.str();
  std::cout << "url = \"" << url << "\"" << std::endl;
  return ses.GetRawPacket(url, &raw_packet, false);
}
