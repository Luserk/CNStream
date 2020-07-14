/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#ifndef CNSTREAM_FRAME_VA_HPP_
#define CNSTREAM_FRAME_VA_HPP_

/**
 *  @file cnstream_frame_va.hpp
 *
 *  This file contains a declaration of the CNFrameData & CNInferObject struct and its subtructure.
 */
#ifdef HAVE_OPENCV
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#if (CV_MAJOR_VERSION >= 3)
#include "opencv2/imgcodecs/imgcodecs.hpp"
#endif
#endif

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cnstream_common.hpp"
#include "cnstream_syncmem.hpp"
#include "util/cnstream_unordered_map.hpp"
#include "util/cnstream_vector.hpp"

#ifndef CN_MAX_PLANES
#define CN_MAX_PLANES 6
#endif

#ifndef ROUND_UP
#define ROUND_UP(addr, boundary) (((u32_t)(addr) + (boundary)-1) & ~((boundary)-1))
#endif

namespace cnstream {
/**
 * An enumerated type that is used to
 * identify the pixel format of the data in CNDataFrame.
 */
typedef enum {
  CN_INVALID = -1,                  ///< This frame is invalid.
  CN_PIXEL_FORMAT_YUV420_NV21 = 0,  ///< This frame is in the YUV420SP(NV21) format.
  CN_PIXEL_FORMAT_YUV420_NV12,      ///< This frame is in the YUV420sp(NV12) format.
  CN_PIXEL_FORMAT_BGR24,            ///< This frame is in the BGR24 format.
  CN_PIXEL_FORMAT_RGB24             ///< This frame is in the RGB24 format.
} CNDataFormat;

/**
 * Identifies if the CNDataFrame data is allocated by CPU or MLU.
 */
typedef struct {
  enum DevType {
    INVALID = -1,        ///< Invalid device type.
    CPU = 0,             ///< The data is allocated by CPU.
    MLU = 1,             ///< The data is allocated by MLU.
    MLU_CPU = 2          ///< Both MLU and CPU. Used for M220_SOC.
  } dev_type = INVALID;  ///< Device type.
  int dev_id = 0;        ///< Ordinal device ID.
  int ddr_channel = 0;   ///< Ordinal channel ID for MLU. The value should be in the range [0, 4).
} DevContext;

/**
 * Identifies memory shared type for multi-process.
 */
enum MemMapType {
  MEMMAP_INVALID = 0,  ///< Invalid memory shared type.
  MEMMAP_CPU = 1,      ///< CPU memory is shared.
  MEMMAP_MLU = 2       ///< MLU memory is shared.
};

/**
 * Gets image plane number by a specified image format.
 *
 * @param
 *   fmt The format of the image.
 *
 * @return
 * @retval 0: Unsupported image format.
 * @retval >0: Image plane number.
 */
inline int CNGetPlanes(CNDataFormat fmt) {
  switch (fmt) {
    case CN_PIXEL_FORMAT_BGR24:
    case CN_PIXEL_FORMAT_RGB24:
      return 1;
    case CN_PIXEL_FORMAT_YUV420_NV12:
    case CN_PIXEL_FORMAT_YUV420_NV21:
      return 2;
    default:
      return 0;
  }
  return 0;
}

/**
 * Dedicated deallocator the CNDecoder buffer.
 */
class IDataDeallocator {
 public:
  virtual ~IDataDeallocator() {}
};

/**
 * ICNMediaImageMapper is an abstract class.
 */
class ICNMediaImageMapper {
 public:
  /**
   * Gets an image.
   * @return Returns the image address.
   */
  virtual void* GetMediaImage() = 0;
  /**
   * Gets pitch.
   * @param index
   * @return Returns pitch.
   */
  virtual int GetPitch(int index) = 0;
  /**
   * Gets CPU address.
   * @param index
   * @return Returns the CPU address.
   */
  virtual void* GetCpuAddress(int index) = 0;
  /**
   * Gets the device address.
   * @param index
   * @return Returns the device address.
   */
  virtual void* GetDevAddress(int index) = 0;
  /**
   *  Destructor of class ICNMediaImageMapper.
   */
  virtual ~ICNMediaImageMapper() {}
};

/**
 * The structure holding a data frame and the frame description.
 */
struct CNDataFrame {
  uint64_t frame_id = -1;  ///< The frame index that incremented from 0.

  /**
   * The source data information. You need to set the information before calling CopyToSyncMem().
   */
  CNDataFormat fmt;                                          ///< The format of the frame.
  int width;                                                 ///< The width of the frame.
  int height;                                                ///< The height of the frame.
  int stride[CN_MAX_PLANES];                                 ///< The strides of the frame.
  DevContext ctx;                                            ///< The device context of this frame.
  void* ptr_mlu[CN_MAX_PLANES];                              ///< The MLU data addresses for planes.
  void* ptr_cpu[CN_MAX_PLANES];                              ///< The CPU data addresses for planes.
  std::shared_ptr<IDataDeallocator> deAllocator_ = nullptr;  ///< The dedicated deallocator for CNDecoder Buffer.
  std::shared_ptr<ICNMediaImageMapper> mapper_ = nullptr;    ///< The dedicated Mapper for M220 CNDecoder.

  CNDataFrame() {}

  ~CNDataFrame();

  /**
   * Gets plane count for a specified frame.
   *
   * @return Returns the plane count of this frame.
   */
  int GetPlanes() const { return CNGetPlanes(fmt); }

  /**
   * Gets the number of bytes in a specified plane.
   *
   * @param plane_idx The index of the plane. The index increments from 0.
   *
   * @return Returns the number of bytes in the plane.
   */
  size_t GetPlaneBytes(int plane_idx) const;

  /**
   * Gets the number of bytes in a frame.
   *
   * @return Returns the number of bytes in a frame.
   */
  size_t GetBytes() const;

  /**
   * Syncronizes the source-data to CNSyncedMemory.
   */
  void CopyToSyncMem();

  /**
   * @brief Syncronizes source data to specific device, and reset ctx.dev_id to device_id when synced, for multi-device
   * case.
   * @param device_id The device id.
   * @return Void.
   */
  void CopyToSyncMemOnDevice(int device_id);

  /**
   * @brief Maps shared memory for multi-process.
   * @param memory The type of the mapped or shared memory.
   * @return Void.
   */
  void MmapSharedMem(MemMapType type, std::string stream_id);

  /**
   * @brief Unmaps the shared memery for multi-process.
   * @param memory The type of the mapped or shared memory.
   * @return Void.
   */
  void UnMapSharedMem(MemMapType type);

  /**
   * @brief Copies source-data to shared memery for multi-process.
   * @param memory The type of the mapped or shared memory.
   * @return Void.
   */
  void CopyToSharedMem(MemMapType type, std::string stream_id);

  /**
   * @brief Releases shared memery for multi-process.
   * @param memory The type of the mapped or shared memory.
   * @return Void.
   */
  void ReleaseSharedMem(MemMapType type, std::string stream_id);

  void* mlu_mem_handle = nullptr;  ///< The MLU memory handle for MLU data.

 public:
  void* cpu_data = nullptr;  ///< CPU data pointer. You need to allocate it by calling CNStreamMallocHost().
  void* mlu_data = nullptr;  ///< A pointer to the MLU data.
  std::shared_ptr<CNSyncedMemory> data[CN_MAX_PLANES];  ///< Sync data helper.

#ifdef HAVE_OPENCV
  /**
   * Converts data from RGB to BGR. Called after CopyToSyncMem() is invoked.
   *
   * If data is not RGB image but BGR, YUV420NV12 or YUV420NV21 image, its color mode will not be converted.
   *
   * @return Returns data with opencv mat type.
   */
  cv::Mat* ImageBGR();

 private:
  cv::Mat* bgr_mat = nullptr;
#endif

 private:
  void* shared_mem_ptr = nullptr;  ///< A pointer to the shared memory for MLU or CPU.
  void* map_mem_ptr = nullptr;     ///< A pointer to the mapped memory for MLU or CPU.
  int shared_mem_fd = -1;          ///< A pointer to the shared memory file descriptor for CPU shared memory.
  int map_mem_fd = -1;             ///< A pointer to the mapped memory file descriptor for CPU mapped memory.
  std::mutex mtx;
};                                 // struct CNDataFrame

/**
 * A structure holding the bounding box for detection information of an object.
 * Normalized coordinates.
 */
struct CNInferBoundingBox {
  float x, y, w, h;
};

/**
 * A structure holding the classification properties of an object.
 */
typedef struct {
  int id = -1;      ///< The unique ID of the classification. The value -1 is invalid.
  int value = -1;   ///< The label value of the classification.
  float score = 0;  ///< The label score of the classification.
} CNInferAttr;

/**
 * The feature value for one object.
 */
struct CNInferFeature {
  std::shared_ptr<float> data;
  size_t size;
};

/**
 * A structure holding the information for an object.
 */
struct CNInferObject {
 public:
  std::string id;           ///< The ID of the classification. (label value).
  std::string track_id;     ///< The tracking result.
  float score;              ///< The label score.
  CNInferBoundingBox bbox;  ///< The object normalized coordinates.

  /**
   * Adds the key of an attribute to a specified object.
   *
   * @param key The Key of the attribute you want to add to. See GetAttribute().
   * @param value The value of the attribute.
   *
   * @return Returns true if the attribute has been added successfully. Returns false if the attribute
   *         already exists.
   *
   * @note This is a thread-safe function.
   */
  bool AddAttribute(const std::string& key, const CNInferAttr& value);

  /**
   * Adds the key pairs of an attribute to a specified object.
   *
   * @param attribute The attribute pair (key, value) to be added.
   *
   * @return Returns true if the attribute has been added successfully. Returns false if the attribute
   *         already exists.
   *
   * @note This is a thread-safe function.
   */
  bool AddAttribute(const std::pair<std::string, CNInferAttr>& attribute);

  /**
   * Gets an attribute by key.
   *
   * @param key The key of an attribute you want to query. See AddAttribute().
   *
   * @return Returns the attribute key. If the attribute
   *         does not exist, CNInferAttr::id will be set to -1.
   *
   * @note This is a thread-safe function.
   */
  CNInferAttr GetAttribute(const std::string& key);

  /**
   * Adds the key of the extended attribute to a specified object.
   *
   * @param key The key of an attribute. You can get this attribute by key. See GetExtraAttribute().
   * @param value The value of the attribute.
   *
   * @return Returns true if the attribute has been added successfully. Returns false if the attribute
   *        already exists in the object.
   *
   * @note This is a thread-safe function.
   */
  bool AddExtraAttribute(const std::string& key, const std::string& value);

  /**
   * Adds the key pairs of the extended attributes to a specified object.
   *
   * @param attributes Attributes to be added.
   *
   * @return Returns true if the attribute has been added successfully. Returns false if the attribute
   *         already exists.
   * @note This is a thread-safe function.
   */
  bool AddExtraAttribute(const std::vector<std::pair<std::string, std::string>>& attributes);

  /**
   * Gets the extended attribute by key.
   *
   * @param key The key of an identified attribute. See AddExtraAttribute().
   *
   * @return Returns the attribute that is identified by the key. If the attribute
   *         does not exist, returns NULL.
   *
   * @note This is a thread-safe function.
   */
  std::string GetExtraAttribute(const std::string& key);

  /**
   * Adds the feature value to a specified object.
   *
   * @param features The feature value you want to add to.
   *
   * @return Void.
   *
   * @note This is a thread-safe function.
   */
  void AddFeature(const CNInferFeature& features);

  /**
   * Gets the features of an object.
   *
   * @return Returns the features of an object.
   *
   * @note This is a thread-safe function.
   */
  ThreadSafeVector<CNInferFeature> GetFeatures();

  void* user_data_ = nullptr;  ///< User data. You can store your own data in this parameter.

 private:
  ThreadSafeUnorderedMap<std::string, CNInferAttr> attributes_;
  ThreadSafeUnorderedMap<std::string, std::string> extra_attributes_;
  ThreadSafeVector<CNInferFeature> features_;
};

/*
 * user-defined data structure: Key-value
 *   key type-- int
 *   value type -- cnstream::any
 */
static constexpr int CNDataFramePtrKey = 0;
using CNDataFramePtr = std::shared_ptr<CNDataFrame>;

static constexpr int CNObjsVecKey = 1;
using CNObjsVec = ThreadSafeVector<std::shared_ptr<CNInferObject>>;

}  // namespace cnstream

#endif  // CNSTREAM_FRAME_VA_HPP_