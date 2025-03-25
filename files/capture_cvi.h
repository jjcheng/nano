//
// Copyright (C) 2024 nihui
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//         http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef CAPTURE_CVI_H
#define CAPTURE_CVI_H

#include <vector>

namespace cv {

class capture_cvi_impl;
class capture_cvi
{
public:
    static bool supported();

    capture_cvi();
    ~capture_cvi();

    int open(int width = 1920, int height = 1080, float fps = 30);

    int get_width() const;
    int get_height() const;
    float get_fps() const;

    int start_streaming();

    //modified by jj
    int read_frame(unsigned char* bgrdata, bool retain_image_ptr);

    int get_pipe_frame(unsigned char* bgrdata);

    int stop_streaming();

    int close();

    //added by jj
    void* getImagePtr();
    //added by jj
    void releaseImagePtr();

private:
    capture_cvi_impl* const d;
    //added by jj
    void* image_ptr;
};

} // namespace cv

#endif // CAPTURE_CVI_H
