// ======================================================================== //
// Copyright 2016 SURVICE Engineering Company                               //
// Copyright 2017 Intel Corporation                                         //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include <fstream>
#include <stdexcept>
#include <string>

#include <algorithm>
#include <iterator>
#include <sstream>
#include <vector>

#include "CameraParser.h"

//#include <ospray/common/Data.h>

std::vector<float> parseline(const std::string &str) {
  std::istringstream iss(str);
  std::vector<float> elems;
  std::string t;
  while (iss >> t) elems.push_back(atof(t.c_str()));
  return elems;
}

bool DefaultCameraParser::parse(int ac, const char **&av) {
  for (int i = 1; i < ac; i++) {
    const std::string arg = av[i];
    if (arg == "--camera" || arg == "-c") {
      cameraType = av[++i];
    } else if (arg == "-v" || arg == "--view") {
      std::ifstream fin(av[++i]);
      if (!fin.is_open()) {
        throw std::runtime_error("Failed to open \"" + std::string(av[i]) +
                                 "\" for reading");
      }

      auto token = std::string("");
      while (fin >> token) {
        if (token == "-vp")
          fin >> eye.x >> eye.y >> eye.z;
        else if (token == "-vu")
          fin >> up.x >> up.y >> up.z;
        else if (token == "-vi")
          fin >> gaze.x >> gaze.y >> gaze.z;
        else if (token == "-fv")
          fin >> fovy;
        else
          throw std::runtime_error("Unrecognized token:  \"" + token + '\"');
      }

    } else if (arg == "-vp" || arg == "--eye") {
      eye.x = atof(av[++i]);
      eye.y = atof(av[++i]);
      eye.z = atof(av[++i]);
    } else if (arg == "-vu" || arg == "--up") {
      up.x = atof(av[++i]);
      up.y = atof(av[++i]);
      up.z = atof(av[++i]);
    } else if (arg == "-vi" || arg == "--gaze") {
      gaze.x = atof(av[++i]);
      gaze.y = atof(av[++i]);
      gaze.z = atof(av[++i]);
    } else if (arg == "-fv" || arg == "--fovy") {
      fovy = atof(av[++i]);
    } else if (arg == "-cf" || arg == "--camerafile") {
      std::string file = std::string(av[++i]);
      std::string line;
      std::ifstream myfile(file);
      // ccount = 0;
      if (myfile.is_open()) {
        std::vector<ospcommon::vec3f> vpos;
        std::vector<ospcommon::vec3f> vdir;
        std::vector<ospcommon::vec3f> vup;
        std::vector<float> vfov;

        while (getline(myfile, line)) {
          if (line[0] == '#') continue;
          std::vector<float> elems = parseline(line);

          if (elems.size() > 3) {
            ospcommon::vec3f v(elems[0], elems[1], elems[2]);
            vpos.push_back(v);
          }

          if (elems.size() > 6) {
            vdir.push_back(ospcommon::vec3f(elems[3], elems[4], elems[5]));
          }

          if (elems.size() > 8) {
            vup.push_back(ospcommon::vec3f(elems[6], elems[7], elems[8]));
          }

          if (elems.size() > 9) {
            vfov.push_back(elems[9]);
          }
          // ccount++;
        }

        for (int c = 0; c < vpos.size(); c++) {
          vdir[c] = vdir[c] - vpos[c];
        }

        ppos = ospNewData(vpos.size(), OSP_FLOAT3, &vpos[0]);
        pdir = ospNewData(vdir.size(), OSP_FLOAT3, &vdir[0]);
        pup = ospNewData(vup.size(), OSP_FLOAT3, &vup[0]);
        pfov = ospNewData(vfov.size(), OSP_FLOAT, &vfov[0]);

        myfile.close();
      }
    } else if (arg == "-cc" || arg == "--cameracount") {
      ccount = ospcommon::vec2i(atoi(av[++i]), atoi(av[++i]));
    }
  }

finalize();

return true;
}

ospray::cpp::Camera DefaultCameraParser::camera() { return parsedCamera; }

void DefaultCameraParser::finalize() {
  if (cameraType.empty()) cameraType = "perspective";

  parsedCamera = ospray::cpp::Camera(cameraType.c_str());
  parsedCamera.set("pos", eye);
  parsedCamera.set("up", up);
  parsedCamera.set("dir", gaze - eye);
  parsedCamera.set("fovy", fovy);

  parsedCamera.set("cx", ccount.x);
  parsedCamera.set("cy", ccount.y);

  parsedCamera.set("vpos", ppos);
  parsedCamera.set("vdir", pdir);
  parsedCamera.set("vup", pup);
  parsedCamera.set("vfov", pfov);

  parsedCamera.commit();
}
