# ShaderVG

<img src="/examples/test_tiger_shader.gif?raw=true" width="400px">

_**Note:** This project is based on https://github.com/ileben/ShivaVG and https://github.com/tqm-dev/ShaderVG_

## Main Features

- Working on Shader-Based OpenGL
- API extensions for GLSL shader integrated to vector/image rendering 

## Getting Started

### Prerequisites

- OpenGL and EGL development libraries and headers should be installed.
- X11 development libraries and headers are needed for the example window harness on Unix-like systems.
- jpeglib is needed for the JPEG-backed image and pattern examples.
- libpng is needed for the PNG-backed blending example.

### Compiling

Clone and enter the repository:
```
$ git clone https://github.com/Boobies/ShaderVG
$ cd ShaderVG
```

Under UNIX systems, execute configure and make:
```
$ sh autogen.sh
$ ./configure
$ make
```

ShaderVG installs two libraries: `libOpenVG` contains the OpenVG implementation,
and `libShaderVGEGL` exposes the EGL entry points that bind OpenVG contexts to
real platform EGL displays and surfaces. Client applications should link
`libShaderVGEGL` before the platform EGL library so ShaderVG's OpenVG-aware EGL
entry points are used first.

### Testing

Move to examples directory, execute tests:
```
$ cd examples
$ ./test_tiger_shader
```
#### test_tiger_shader
  Well known svg tiger meets GLSL vertex/fragment shading. 

#### test_vgu
  Constructs some path primitives using the VGU API.

#### test_warp
  Renders the tiger artwork into a VGImage and demonstrates the VGU
  image warp helpers.

#### test_tiger
  The most simple performance test. It draws the well known svg
  tiger using just simple stroke and fill of solid colors. It
  consists of 240 paths.

#### test_dash
  Shows different stroke dashing modes.

#### test_linear
  A rectangle drawn using 3-color linear gradient fill paint

#### test_radial
  A rectangle drawn using 3-color radial gradient fill paint

#### test_interpolate
  Interpolates between two paths - an apple and a pear.

#### test_paths
  Demonstrates path metrics by animating a marker and tangent indicators along
  a dashed curve, while also showing path interpolation and bounds overlays.

#### test_image
  Images are drawn using VG_DRAW_IMAGE_MULTIPLY image mode to be
  multiplied with radial gradient fill paint.

#### test_pattern
  An image is drawn in multiply mode with an image pattern fill
  paint.

#### test_blend
  Loads PNG source and destination images and draws them with the standard
  OpenVG blend modes.

#### test_advanced_blend
  Shows `VG_KHR_advanced_blending` using vector source and destination panels
  in the same order as the extension blend-mode tokens.

#### test_alpha
  Shows source-over alpha blending with overlapping translucent red, green,
  and blue circles.

#### test_antialias
  Compares `VG_RENDERING_QUALITY_NONANTIALIASED`,
  `VG_RENDERING_QUALITY_FASTER`, and `VG_RENDERING_QUALITY_BETTER` side by
  side using filled curves and strokes.

#### test_pbuffer
  Minimal EGL/OpenVG pbuffer smoke test that clears an offscreen surface
  and reads one pixel back. It also covers OpenVG image-backed pbuffers
  created with `eglCreatePbufferFromClientBuffer`.

#### test_egl_gl_vg
  Interleaves raw OpenGL and OpenVG drawing on the same EGL surface.

#### test_egl_features
  Demonstrates OpenVG EGL context sharing, rendering into a `VGImage` pbuffer,
  and using the shared surface mask from a second OpenVG context.

#### test_masking
  Demonstrates image masks, mask layers, `vgRenderToMask`, and saving/restoring
  surface mask state with `vgCopyMask`.

#### test_pixels
  Exercises image upload, image clear/copy/readback, drawing-surface pixel
  writes, reads, and overlapping surface copies using synthetic pixel data.

#### test_filters
  Shows the OpenVG image filter APIs on synthetic image data.

#### test_parametric_filter
  Shows `VG_KHR_parametric_filter` effects, including drop shadow, bevel, and
  gradient-based variants.

#### test_font
  Draws vector glyphs through the OpenVG font API, including glyphs whose
  source paths have already been destroyed by the application.

### Conformance testing

Khronos publishes the OpenVG Conformance Test Suite at
https://github.com/KhronosGroup/OpenVG-CTS. ShaderVG does not vendor that tree
or run it as part of the normal build, but `tools/run_openvg_cts.sh` can build
the OpenVG 1.1 CTS generation harness against this checkout:

```
$ tools/run_openvg_cts.sh --clone --build-only
$ tools/run_openvg_cts.sh --cts-dir /tmp/OpenVG-CTS --test A10101
$ tools/run_openvg_cts.sh --cts-dir /tmp/OpenVG-CTS \
    --list-file tools/openvg_cts_smoke.txt --config 1 --verbose 2
```

The script stores generated answers, config info, and logs under
`cts-results/`, which is ignored by Git. It links the CTS generator against the
uninstalled `src/.libs/libShaderVGEGL.so` and `src/.libs/libOpenVG.so`, with
ShaderVG's EGL entry points ordered before the platform EGL library.

`tools/openvg_cts_smoke.txt` tracks the CTS groups expected to pass today.
`tools/openvg_cts_blocked.txt` lists useful groups that currently expose known
compliance gaps and should be promoted into the smoke list as those gaps close.

Use CTS results as development triage until the remaining TODO items are closed.
The public CTS is useful for local testing, but official OpenVG conformance and
trademark claims require Khronos' adopter process:
https://www.khronos.org/conformance/

## Implementation status

#### General                                                        
API                     | status                                    
----------------------- | ---------------------                     
vgGetError              | FULLY implemented                         
vgFlush                 | FULLY implemented                         
vgFinish                | FULLY implemented                         
                                                                    
#### Getters and setters                                            
API                     | status                                    
----------------------- | ---------------------                     
vgSet                   | FULLY implemented                         
vgSeti                  | FULLY implemented                         
vgSetfv                 | FULLY implemented                         
vgSetiv                 | FULLY implemented                         
vgGetf                  | FULLY implemented                         
vgGeti                  | FULLY implemented                         
vgGetVectorSize         | FULLY implemented                         
vgGetfv                 | FULLY implemented                         
vgGetiv                 | FULLY implemented                         
vgSetParameterf         | FULLY implemented                         
vgSetParameteri         | FULLY implemented                         
vgSetParameterfv        | FULLY implemented                         
vgSetParameteriv        | FULLY implemented                         
vgGetParameterf         | FULLY implemented                         
vgGetParameteri         | FULLY implemented                         
vgGetParameterVectorSize| FULLY implemented                         
vgGetParameterfv        | FULLY implemented                         
vgGetParameteriv        | FULLY implemented                         
                                                                    
#### Matrix Manipulation                                            
API                     | status                                    
----------------------- | ---------------------                     
vgLoadIdentity          | FULLY implemented                         
vgLoadMatrix            | FULLY implemented                         
vgGetMatrix             | FULLY implemented                         
vgMultMatrix            | FULLY implemented                         
vgTranslate             | FULLY implemented                         
vgScale                 | FULLY implemented                         
vgShear                 | FULLY implemented                         
vgRotate                | FULLY implemented                         
                                                                    
#### Masking and Clearing                                       
API                     | status                                
----------------------- | ---------------------                 
vgMask                  | FULLY implemented
vgRenderToMask          | FULLY implemented
vgCreateMaskLayer       | FULLY implemented
vgDestroyMaskLayer      | FULLY implemented
vgFillMaskLayer         | FULLY implemented
vgCopyMask              | FULLY implemented
vgClear                 | FULLY implemented                     
                                                                
#### Paths                                                      
API                     | status                                
----------------------- | ---------------------                 
vgCreatePath            | FULLY implemented                     
vgClearPath             | FULLY implemented                     
vgDestroyPath           | FULLY implemented                     
vgRemovePathCapabilities| FULLY implemented                     
vgGetPathCapabilities   | FULLY implemented                     
vgAppendPath            | FULLY implemented                     
vgAppendPathData        | FULLY implemented                     
vgModifyPathCoords      | FULLY implemented                     
vgTransformPath         | FULLY implemented                     
vgInterpolatePath       | FULLY implemented                     
vgPathLength            | FULLY implemented
vgPointAlongPath        | FULLY implemented
vgPathBounds            | FULLY implemented                     
vgPathTransformedBounds | FULLY implemented                     
vgDrawPath              | PARTIALLY implemented                 
                                                                
#### Paint                                                      
API                     | status                                
----------------------- | ---------------------                 
vgCreatePaint           | FULLY implemented                     
vgDestroyPaint          | FULLY implemented                     
vgSetPaint              | FULLY implemented                     
vgGetPaint              | FULLY implemented                     
vgSetColor              | FULLY implemented                     
vgGetColor              | FULLY implemented                     
vgPaintPattern          | FULLY implemented             

#### Images                                        
API                     | status                   
----------------------- | ---------------------    
vgCreateImage           | PARTIALLY implemented    
vgDestroyImage          | FULLY implemented        
vgClearImage            | FULLY implemented        
vgImageSubData          | PARTIALLY implemented    
vgGetImageSubData       | PARTIALLY implemented    
vgChildImage            | NOT implemented          
vgGetParent             | NOT implemented          
vgCopyImage             | FULLY implemented        
vgDrawImage             | PARTIALLY implemented    
vgSetPixels             | FULLY implemented        
vgWritePixels           | FULLY implemented        
vgGetPixels             | FULLY implemented        
vgReadPixels            | FULLY implemented        
vgCopyPixels            | FULLY implemented        

#### Text
API                     | status
----------------------- | ---------------------
vgCreateFont            | FULLY implemented
vgDestroyFont           | FULLY implemented
vgSetGlyphToPath        | FULLY implemented
vgSetGlyphToImage       | FULLY implemented
vgClearGlyph            | FULLY implemented
vgDrawGlyph             | FULLY implemented
vgDrawGlyphs            | FULLY implemented
                                                   
#### Image Filters                                 
API                     | status                   
----------------------- | ---------------------    
vgColorMatrix           | FULLY implemented
vgConvolve              | FULLY implemented
vgSeparableConvolve     | FULLY implemented
vgGaussianBlur          | FULLY implemented
vgLookup                | FULLY implemented
vgLookupSingle          | FULLY implemented
vgParametricFilterKHR   | FULLY implemented
                                                   
#### Queries                                       
API                     | status                   
----------------------- | ---------------------    
vgHardwareQuery         | FULLY implemented
vgGetString             | FULLY implemented        
                                                   
#### VGU                                          
API                        | status               
-----------------------    | ---------------------
vguLine                    | FULLY implemented    
vguPolygon                 | FULLY implemented    
vguRect                    | FULLY implemented    
vguRoundRect               | FULLY implemented    
vguEllipse                 | FULLY implemented    
vguArc                     | FULLY implemented    
vguComputeWarpQuadToSquare | FULLY implemented
vguComputeWarpSquareToQuad | FULLY implemented
vguComputeWarpQuadToQuad   | FULLY implemented
vguDropShadowKHR           | FULLY implemented
vguGlowKHR                 | FULLY implemented
vguBevelKHR                | FULLY implemented
vguGradientGlowKHR         | FULLY implemented
vguGradientBevelKHR        | FULLY implemented
        
## Extensions

### OpenVG registry extension status

The Khronos OpenVG registry lists these OpenVG extensions:
https://registry.khronos.org/OpenVG/

ShaderVG advertises only extensions marked as fully implemented through
`vgGetString(VG_EXTENSIONS)`.

Extension/API                         | status
------------------------------------- | ---------------------
VG_KHR_EGL_image                      | NOT implemented
vgCreateEGLImageTargetKHR             | NOT implemented
VG_KHR_iterative_average_blur         | FULLY implemented
vgIterativeAverageBlurKHR             | FULLY implemented
VG_KHR_advanced_blending              | FULLY implemented
VG_BLEND_*_KHR advanced blend modes   | FULLY implemented
VG_NDS_paint_generation               | NOT implemented
VG_PAINT_COLOR_RAMP_LINEAR_NDS        | NOT implemented
VG_COLOR_MATRIX_NDS                   | NOT implemented
VG_PAINT_COLOR_TRANSFORM_LINEAR_NDS   | NOT implemented
VG_DRAW_IMAGE_COLOR_MATRIX_NDS        | NOT implemented
VG_KHR_parametric_filter              | FULLY implemented
vgParametricFilterKHR                 | FULLY implemented
vguDropShadowKHR                      | FULLY implemented
vguGlowKHR                            | FULLY implemented
vguBevelKHR                           | FULLY implemented
vguGradientGlowKHR                    | FULLY implemented
vguGradientBevelKHR                   | FULLY implemented
VG_NDS_projective_geometry            | NOT implemented
vgProjectiveMatrixNDS                 | NOT implemented
vguTransformClipLineNDS               | NOT implemented
VG_CLIP_*_NDS state                   | NOT implemented
VG_RQUAD_TO_*_NDS path commands       | NOT implemented
VG_RCUBIC_TO_*_NDS path commands      | NOT implemented
VG_KHR_EGL_sync                       | NOT implemented
eglCreateSyncKHR                      | NOT implemented
eglDestroySyncKHR                     | NOT implemented
eglClientWaitSyncKHR                  | NOT implemented
eglGetSyncAttribKHR                   | NOT implemented

### EGL OpenVG context binding

ShaderVG clients create and bind OpenVG contexts through EGL:

```c
EGLDisplay dpy = eglGetDisplay(native_display);
eglInitialize(dpy, NULL, NULL);
eglBindAPI(EGL_OPENVG_API);

EGLint attribs[] = {
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
  EGL_NONE
};
eglChooseConfig(dpy, attribs, &config, 1, &count);

surface = eglCreateWindowSurface(dpy, config, native_window, NULL);
context = eglCreateContext(dpy, config, EGL_NO_CONTEXT, NULL);
eglMakeCurrent(dpy, surface, surface, context);

vgClear(0, 0, width, height);
eglSwapBuffers(dpy, surface);
```

`libShaderVGEGL` delegates native display, surface, and backing OpenGL context
creation to the platform EGL implementation. ShaderVG only supplies the OpenVG
implementation and the glue needed for `EGL_OPENVG_API` / `EGL_OPENVG_BIT` to
select a ShaderVG OpenVG context. OpenVG-capable configs expose
`EGL_OPENVG_BIT` through `EGL_RENDERABLE_TYPE` and `EGL_CONFORMANT`, and
advertise `EGL_ALPHA_MASK_SIZE == 8`, matching ShaderVG's 8-bit GPU mask
surface.

ShaderVG supports antialiasing through `VG_RENDERING_QUALITY` on single-sample
surfaces, but applications should prefer EGL multisampled surfaces when
available by requesting `EGL_SAMPLE_BUFFERS` and `EGL_SAMPLES`. Native EGL MSAA
is generally the best-quality and most efficient antialiasing path.

`eglCreatePbufferFromClientBuffer` supports `EGL_OPENVG_IMAGE` client buffers.
The resulting pbuffer renders directly into the supplied `VGImage`, can only be
bound to the OpenVG context that created it, and follows the OpenVG in-use rules
while it is current. Texture-bound EGL pbuffer attributes are rejected for these
surfaces because ShaderVG exposes them as OpenVG render targets rather than EGL
texture surfaces.

## License

This project is licensed under the GNU Lesser General Public License v2.1 - see the [LICENSE](https://github.com/Boobies/ShaderVG/blob/master/COPYING) file for details
