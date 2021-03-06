//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// mtl_resources.h:
//    Declares wrapper classes for Metal's MTLTexture and MTLBuffer.
//

#ifndef LIBANGLE_RENDERER_METAL_MTL_RESOURCES_H_
#define LIBANGLE_RENDERER_METAL_MTL_RESOURCES_H_

#import <Metal/Metal.h>

#include <atomic>
#include <memory>

#include "common/FastVector.h"
#include "common/MemoryBuffer.h"
#include "common/angleutils.h"
#include "libANGLE/Error.h"
#include "libANGLE/ImageIndex.h"
#include "libANGLE/angletypes.h"
#include "libANGLE/renderer/metal/mtl_common.h"
#include "libANGLE/renderer/metal/mtl_format_utils.h"

namespace rx
{

class ContextMtl;

namespace mtl
{

class CommandQueue;
class BlitCommandEncoder;
class Resource;
class Texture;
class Buffer;

using ResourceRef    = std::shared_ptr<Resource>;
using TextureRef     = std::shared_ptr<Texture>;
using TextureWeakRef = std::weak_ptr<Texture>;
using BufferRef      = std::shared_ptr<Buffer>;
using BufferWeakRef  = std::weak_ptr<Buffer>;

class Resource : angle::NonCopyable
{
  public:
    virtual ~Resource() {}

    // Check whether the resource still being used by GPU
    bool isBeingUsedByGPU(Context *context) const;
    // Checks whether the last command buffer that uses the given resource has been committed or not
    bool hasPendingWorks(Context *context) const;

    void setUsedByCommandBufferWithQueueSerial(uint64_t serial, bool writing);

    uint64_t getCommandBufferQueueSerial() const { return mUsageRef->cmdBufferQueueSerial; }

    // Flag indicate whether we should synchronize the content to CPU after GPU changed this
    // resource's content.
    bool isCPUReadMemNeedSync() const { return mUsageRef->cpuReadMemNeedSync; }
    void resetCPUReadMemNeedSync() { mUsageRef->cpuReadMemNeedSync = false; }

  protected:
    Resource();
    // Share the GPU usage ref with other resource
    Resource(Resource *other);

    void reset();

  private:
    struct UsageRef
    {
        // The id of the last command buffer that is using this resource.
        uint64_t cmdBufferQueueSerial = 0;

        // This flag means the resource was issued to be modified by GPU, if CPU wants to read
        // its content, explicit synchronization call must be invoked.
        bool cpuReadMemNeedSync = false;
    };

    // One resource object might just be a view of another resource. For example, a texture 2d
    // object might be a view of one face of a cube texture object. Another example is one texture
    // object of size 2x2 might be a mipmap view of a texture object size 4x4. Thus, if one object
    // is being used by a command buffer, it means the other object is being used also. In this
    // case, the two objects must share the same UsageRef property.
    std::shared_ptr<UsageRef> mUsageRef;
};

class Texture final : public Resource,
                      public WrappedObject<id<MTLTexture>>,
                      public std::enable_shared_from_this<Texture>
{
  public:
    static angle::Result Make2DTexture(ContextMtl *context,
                                       const Format &format,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t mips /** use zero to create full mipmaps chain */,
                                       bool renderTargetOnly,
                                       bool allowFormatView,
                                       TextureRef *refOut);

    static angle::Result MakeCubeTexture(ContextMtl *context,
                                         const Format &format,
                                         uint32_t size,
                                         uint32_t mips /** use zero to create full mipmaps chain */,
                                         bool renderTargetOnly,
                                         bool allowFormatView,
                                         TextureRef *refOut);

    static angle::Result Make2DMSTexture(ContextMtl *context,
                                         const Format &format,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t samples,
                                         bool renderTargetOnly,
                                         bool allowFormatView,
                                         TextureRef *refOut);

    static angle::Result Make3DTexture(ContextMtl *context,
                                       const Format &format,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t depth,
                                       uint32_t mips,
                                       bool renderTargetOnly,
                                       bool allowFormatView,
                                       TextureRef *refOut);

    static TextureRef MakeFromMetal(id<MTLTexture> metalTexture);

    // Allow CPU to read & write data directly to this texture?
    bool isCPUAccessible() const;

    bool supportFormatView() const;

    void replace2DRegion(ContextMtl *context,
                         const MTLRegion &region,
                         uint32_t mipmapLevel,
                         uint32_t slice,
                         const uint8_t *data,
                         size_t bytesPerRow);

    void replaceRegion(ContextMtl *context,
                       const MTLRegion &region,
                       uint32_t mipmapLevel,
                       uint32_t slice,
                       const uint8_t *data,
                       size_t bytesPerRow,
                       size_t bytesPer2DImage);

    void getBytes(ContextMtl *context,
                  size_t bytesPerRow,
                  size_t bytesPer2DInage,
                  const MTLRegion &region,
                  uint32_t mipmapLevel,
                  uint32_t slice,
                  uint8_t *dataOut);

    // Create 2d view of a cube face which full range of mip levels.
    TextureRef createCubeFaceView(uint32_t face);
    // Create a view of one slice at a level.
    TextureRef createSliceMipView(uint32_t slice, uint32_t level);
    // Create a view of a level.
    TextureRef createMipView(uint32_t level);
    // Create a view with different format
    TextureRef createViewWithDifferentFormat(MTLPixelFormat format);
    // Same as above but the target format must be compatible, for example sRGB to linear. In this
    // case texture doesn't need format view usage flag.
    TextureRef createViewWithCompatibleFormat(MTLPixelFormat format);

    MTLTextureType textureType() const;
    MTLPixelFormat pixelFormat() const;

    uint32_t mipmapLevels() const;
    uint32_t arrayLength() const;
    uint32_t cubeFacesOrArrayLength() const;

    uint32_t width(uint32_t level = 0) const;
    uint32_t height(uint32_t level = 0) const;
    uint32_t depth(uint32_t level = 0) const;

    gl::Extents size(uint32_t level = 0) const;
    gl::Extents size(const gl::ImageIndex &index) const;

    uint32_t samples() const;

    angle::Result resize(ContextMtl *context, uint32_t width, uint32_t height);

    // For render target
    MTLColorWriteMask getColorWritableMask() const { return *mColorWritableMask; }
    void setColorWritableMask(MTLColorWriteMask mask) { *mColorWritableMask = mask; }

    // Get linear color space view. Only usable for sRGB textures.
    TextureRef getLinearColorView();

    // Get stencil view
    TextureRef getStencilView();

    // Change the wrapped metal object. Special case for swapchain image
    void set(id<MTLTexture> metalTexture);

    // sync content between CPU and GPU
    void syncContent(ContextMtl *context, mtl::BlitCommandEncoder *encoder);

  private:
    using ParentClass = WrappedObject<id<MTLTexture>>;

    static angle::Result MakeTexture(ContextMtl *context,
                                     const Format &mtlFormat,
                                     MTLTextureDescriptor *desc,
                                     uint32_t mips,
                                     bool renderTargetOnly,
                                     bool allowFormatView,
                                     TextureRef *refOut);

    Texture(id<MTLTexture> metalTexture);
    Texture(ContextMtl *context,
            MTLTextureDescriptor *desc,
            uint32_t mips,
            bool renderTargetOnly,
            bool allowFormatView);

    // Create a texture view
    Texture(Texture *original, MTLPixelFormat format);
    Texture(Texture *original, MTLTextureType type, NSRange mipmapLevelRange, NSRange slices);

    void syncContent(ContextMtl *context);

    AutoObjCObj<MTLTextureDescriptor> mCreationDesc;

    // This property is shared between this object and its views:
    std::shared_ptr<MTLColorWriteMask> mColorWritableMask;

    // Linear view of sRGB texture
    TextureRef mLinearColorView;

    TextureRef mStencilView;
};

class Buffer final : public Resource, public WrappedObject<id<MTLBuffer>>
{
  public:
    static angle::Result MakeBuffer(ContextMtl *context,
                                    size_t size,
                                    const uint8_t *data,
                                    BufferRef *bufferOut);

    angle::Result reset(ContextMtl *context, size_t size, const uint8_t *data);

    uint8_t *map(ContextMtl *context);
    void unmap(ContextMtl *context);

    size_t size() const;

  private:
    Buffer(ContextMtl *context, size_t size, const uint8_t *data);
};

}  // namespace mtl
}  // namespace rx

#endif /* LIBANGLE_RENDERER_METAL_MTL_RESOURCES_H_ */
