/* -*-c++-*-
 *  ----------------------------------------------------------------------------
 *
 *       PlantGL: Modeling Plant Geometry
 *
 *       Copyright 2000-2018 - Cirad/Inra/Inria
 *
 *       File author(s): F. Boudon (frederic.boudon@cirad.fr) et al.
 *
 *       Development site : https://github.com/openalea/plantgl
 *
 *  ----------------------------------------------------------------------------
 * 
 *                      GNU General Public Licence
 *           
 *       This program is free software; you can redistribute it and/or
 *       modify it under the terms of the GNU General Public License as
 *       published by the Free Software Foundation; either version 2 of
 *       the License, or (at your option) any later version.
 *
 *       This program is distributed in the hope that it will be useful,
 *       but WITHOUT ANY WARRANTY; without even the implied warranty of
 *       MERCHANTABILITY or FITNESS For A PARTICULAR PURPOSE. See the
 *       GNU General Public License for more details.
 *
 *       You should have received a copy of the GNU General Public
 *       License along with this program; see the file COPYING. If not,
 *       write to the Free Software Foundation, Inc., 59
 *       Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  ----------------------------------------------------------------------------
 */             


/* ----------------------------------------------------------------------- */

#include "zbufferengine.h"
#include "projectionrenderer.h"
#include "projection_util.h"
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <queue>
/* ----------------------------------------------------------------------- */

PGL_USING_NAMESPACE
TOOLS_USING_NAMESPACE

#define DEFAULT_MULTITHREAD true

ZBufferEngine::ZBufferEngine(uint16_t imageWidth, uint16_t imageHeight, const Color3& backGroundColor, eRenderingStyle style):
    ProjectionEngine(),
    __imageWidth(imageWidth), 
    __imageHeight(imageHeight), 
    __lightPosition(0,0,1),  
    __lightAmbient(255,255,255),
    __lightDiffuse(255,255,255),
    __lightSpecular(255,255,255),
    __alphathreshold(0.99),
    __depthBuffer(new RealArray2(uint_t(imageWidth), uint_t(imageHeight), REAL_MAX)),
    __frameBuffer(style != eDepthOnly ? new PglFrameBufferManager(imageWidth, imageHeight, style == eIdBased ? 4 : 3, backGroundColor) : NULL),
    __imageMutex(),
    __triangleshader(NULL),
    __triangleshaderset(NULL),
    __multithreaded(DEFAULT_MULTITHREAD)
{
    if (style != eDepthOnly) {
        if (style == eIdBased) __triangleshader = TriangleShaderPtr(new IdBasedShader(this));
        else __triangleshader = TriangleShaderPtr(new TriangleShaderSelector(this));
    }
}    


ZBufferEngine::ZBufferEngine(uint16_t imageWidth, uint16_t imageHeight, const Color4& backGroundColor, eRenderingStyle style):
    ProjectionEngine(),
    __imageWidth(imageWidth), 
    __imageHeight(imageHeight), 
    __lightPosition(0,0,1),  
    __lightAmbient(255,255,255),
    __lightDiffuse(255,255,255),
    __lightSpecular(255,255,255),
    __alphathreshold(0.99),
    __depthBuffer(new RealArray2(uint_t(imageWidth), uint_t(imageHeight), REAL_MAX)),
    __frameBuffer(style == eColorBased ? new PglFrameBufferManager(imageWidth, imageHeight, 3, backGroundColor) : NULL),
    __imageMutex(),
    __triangleshader(NULL),
    __triangleshaderset(NULL),
    __multithreaded(DEFAULT_MULTITHREAD)
{
    if (style != eDepthOnly) {
        if (style == eIdBased) __triangleshader = TriangleShaderPtr(new IdBasedShader(this, backGroundColor.toUint()));
        else __triangleshader = TriangleShaderPtr(new TriangleShaderSelector(this));
    }
}    
    
ZBufferEngine::ZBufferEngine(uint16_t imageWidth, uint16_t imageHeight,uint32_t defaultid, Color4::eColor4Format conversionformat):
    ProjectionEngine(),
    __imageWidth(imageWidth), 
    __imageHeight(imageHeight), 
    __lightPosition(0,0,1),  
    __lightAmbient(255,255,255),
    __lightDiffuse(255,255,255),
    __lightSpecular(255,255,255),
    __alphathreshold(0.99),
    __depthBuffer(new RealArray2(uint_t(imageWidth), uint_t(imageHeight), REAL_MAX)),
    __frameBuffer(), // will be initialized into IdBasedShader constructor
    __imageMutex(),
    __triangleshader(new IdBasedShader(this, defaultid, conversionformat)),
    __triangleshaderset(NULL),
    __multithreaded(DEFAULT_MULTITHREAD)
{
}    
    
    
ZBufferEngine::~ZBufferEngine()
{
}

void ZBufferEngine::lock(uint_t x, uint_t y)
{
    if(__multithreaded) __imageMutex->lock(x,y);
}

void ZBufferEngine::unlock(uint_t x, uint_t y)
{
    if(__multithreaded) __imageMutex->unlock(x,y);    
}

bool ZBufferEngine::tryLock(uint_t x, uint_t y)
{
    if(__multithreaded) return __imageMutex->tryLock(x,y);
    return true;   
}

void ZBufferEngine::beginProcess()
{

    if (__multithreaded){
        // printf("begin rendering : create thread pool\n");
        threadmanager().init_tasks();
        __imageMutex = threadmanager().getImageMutex(__imageWidth, __imageHeight);
    }

}

void ZBufferEngine::endProcess()
{

    if(__multithreaded){
        threadmanager().join();
        // __pool->join();
        // printf("end rendering : %u\n", uint32_t(__nb_tasks));
        //printf("end rendering done\n");
    }
}


ImagePtr ZBufferEngine::getImage() const
{
    PglFrameBufferManagerPtr fb = dynamic_pointer_cast<PglFrameBufferManager>(__frameBuffer);
    if (is_valid_ptr(fb)) { return fb->getImage(); }
    return ImagePtr();
}


bool ZBufferEngine::isTotallyTransparent(const real_t alpha) const
{
    return alpha >= __alphathreshold;
}


Color3 ZBufferEngine::getFrameBufferAt(uint32_t x, uint32_t y)
{
    if(__frameBuffer) return __frameBuffer->getPixelAt(x,y);
    return Color3(0,0,0);
}

void ZBufferEngine::setFrameBufferAt(uint32_t x, uint32_t y, const Color3& rasterColor)
{
    if(__frameBuffer) __frameBuffer->setPixelAt(x,y,rasterColor);
}

void ZBufferEngine::setFrameBufferAt(uint32_t x, uint32_t y, const Color4& rasterColor)
{
    if (rasterColor.getAlpha() < GEOM_EPSILON )
        setFrameBufferAt(x,y, Color3(rasterColor));
    else if (!isTotallyTransparent(rasterColor.getAlpha())) {
        real_t alpha = rasterColor.getAlphaClamped();
        Color3 current = getFrameBufferAt(x,y);
        Color3 result = current * (1-alpha) + rasterColor * alpha;
        setFrameBufferAt(x,y, result);
    }
}

bool ZBufferEngine::isVisible(int32_t x, int32_t y, real_t z) const
{
    real_t cz = __depthBuffer->getAt(x, y);
    return (z < cz && (cz-z) > GEOM_EPSILON); 
}

bool ZBufferEngine::isVisible(const TOOLS(Vector3)& pos) const
{
    Vector3 raster = worldToRaster(pos);
    return isVisible(raster.x(), raster.y(), raster.z());
}


bool ZBufferEngine::renderRaster(uint32_t x, uint32_t y, real_t z, const Color4& rasterColor)
{
    if (isTotallyTransparent(rasterColor.getAlpha())) return false;

    lock(x,y);
    if (isVisible(x,y,z)) {
        __depthBuffer->setAt(x, y, z);
        setFrameBufferAt(x,y,rasterColor);
        unlock(x,y);
        return true;
    }
    unlock(x,y);

    return false;

}

void ZBufferEngine::setLight(const TOOLS(Vector3)& lightPosition, const Color3& lightColor)
{
    // printf("Set Light Color : %u %u %u\n", lightColor.getRed(), lightColor.getGreen(), lightColor.getBlue());
    __lightPosition = lightPosition;
    __lightAmbient = lightColor;
    __lightDiffuse = lightColor;
    __lightSpecular = lightColor;
}

void ZBufferEngine::setLight(const TOOLS(Vector3)& lightPosition, const Color3& lightAmbient, const Color3& lightDiffuse, const Color3& lightSpecular)
{
    // printf("Set Light Ambient : %u %u %u\n", lightAmbient.getRed(), lightAmbient.getGreen(), lightAmbient.getBlue());
    __lightPosition = lightPosition;
    __lightAmbient = lightAmbient;
    __lightDiffuse = lightDiffuse;
    __lightSpecular = lightSpecular;
}


void ZBufferEngine::iprocess(TriangleSetPtr triangles, AppearancePtr appearance, uint32_t id, ProjectionCameraPtr _camera, uint32_t threadid)
{
    // printf("process TriangleSetPtr\n");
    const Point3ArrayPtr points(triangles->getPointList());
    const Index3ArrayPtr indices(triangles->getIndexList());

    bool ccw = triangles->getCCW();

    triangles->checkNormalList();

    size_t nbfaces = triangles->getIndexListSize();
    bool hasColor = triangles->hasColorList();

    TriangleShaderPtr shader;
    if (threadid != 0) {
        assert(threadid <= threadmanager().nb_threads());
        //printf("session %u\n", threadid);
        shader = __triangleshaderset[threadid-1];
    }
    else {
        shader = __triangleshader;
    }

    // ProjectionCameraPtr camera = (__multithreaded?ProjectionCameraPtr(_camera->copy()):_camera);
    for(uint32_t itidx = 0; itidx < nbfaces; ++itidx){
        //printf("triangle %i on %i\n", itidx, nbfaces);

        const Vector3& v0 = triangles->getFacePointAt(itidx,0);
        const Vector3& v1 = triangles->getFacePointAt(itidx,1);
        const Vector3& v2 = triangles->getFacePointAt(itidx,2);

        if(is_valid_ptr(shader)){
            shader->init(appearance, triangles, itidx, id, _camera);
        }
      
        // shader->initEnv(_camera);
        renderShadedTriangle(v0, v1, v2, ccw, shader, _camera);

    }

}

void ZBufferEngine::renderShadedTriangleMT(const TOOLS(Vector3)& v0, const TOOLS(Vector3)& v1, const TOOLS(Vector3)& v2, bool ccw, const TriangleShaderPtr& shader, const ProjectionCameraPtr& camera)
{
    shader->initEnv(camera);
    renderShadedTriangle(v0, v1, v2, ccw, shader, camera);
    // printf("- rendering nb task : %u\n", uint32_t(__nb_tasks));
    threadmanager().end_task();
}


void ZBufferEngine::renderShadedTriangle(const TOOLS(Vector3)& v0, const TOOLS(Vector3)& v1, const TOOLS(Vector3)& v2, bool ccw, const TriangleShaderPtr& shader,  const ProjectionCameraPtr& camera)
{
    
     // Projection in camera space
    Vector3 v0Cam = camera->worldToCamera(v0);
    Vector3 v1Cam = camera->worldToCamera(v1);
    Vector3 v2Cam = camera->worldToCamera(v2);

    // Convert the vertices of the triangle to raster space
    Vector3 v0Raster = camera->cameraToRaster(v0Cam,__imageWidth, __imageHeight);
    Vector3 v1Raster = camera->cameraToRaster(v1Cam,__imageWidth, __imageHeight);
    Vector3 v2Raster = camera->cameraToRaster(v2Cam,__imageWidth, __imageHeight);

    // Prepare vertex attributes. Divde them by their vertex z-coordinate
    // (though we use a multiplication here because v.z = 1 / v.z)
    //Vec2f st0 = st[stindices[i * 3]];
    //Vec2f st1 = st[stindices[i * 3 + 1]];
    //Vec2f st2 = st[stindices[i * 3 + 2]];
    // st0 *= v0Raster.z, st1 *= v1Raster.z, st2 *= v2Raster.z;

    real_t xmin = min3(v0Raster.x(), v1Raster.x(), v2Raster.x());
    real_t ymin = min3(v0Raster.y(), v1Raster.y(), v2Raster.y());
    real_t zmin = min3(v0Raster.z(), v1Raster.z(), v2Raster.z());

    real_t xmax = max3(v0Raster.x(), v1Raster.x(), v2Raster.x());
    real_t ymax = max3(v0Raster.y(), v1Raster.y(), v2Raster.y());
    real_t zmax = max3(v0Raster.z(), v1Raster.z(), v2Raster.z());


      // the triangle is out of screen
    if (xmin >= __imageWidth  || xmax < 0 || ymin >= __imageHeight || ymax < 0 || !camera->isInZRange(zmin, zmax)) {
        // printf("skip \n");
        // printf("%f %f %f \n", v0Raster.x(), v1Raster.x(), v2Raster.x());
        // printf("%f %f %f \n", v0Raster.y(), v1Raster.y(), v2Raster.y());
        // printf("%f %f %f \n", v0Raster.z(), v1Raster.z(), v2Raster.z());
        return;
    }

    // be careful xmin/xmax/ymin/ymax can be negative. Don't cast to uint32_t
    int32_t x0 = pglMax(int32_t(0), (int32_t)(std::floor(xmin)));
    int32_t x1 = pglMin(int32_t(__imageWidth) - 1, (int32_t)(std::floor(xmax)));
    int32_t y0 = pglMax(int32_t(0), (int32_t)(std::floor(ymin)));
    int32_t y1 = pglMin(int32_t(__imageHeight) - 1, (int32_t)(std::floor(ymax)));

    if (__multithreaded && (x1-x0+1)*(y1-y0+1) > 20) {
        threadmanager().new_task(boost::bind(&ZBufferEngine::rasterizeMT, this, Index4(x0,x1,y0,y1), v0Raster, v1Raster, v2Raster, ccw, TriangleShaderPtr(shader->copy()), ProjectionCameraPtr(camera->copy())));
    }
    else {
        rasterize(x0,x1,y0,y1,v0Raster,v1Raster,v2Raster,ccw,shader,camera);
    }
}




struct Fragment {
    real_t x,y,z;
    real_t w0,w1,w2;
    Fragment(real_t _x,real_t _y,real_t _z,real_t _w0,real_t _w1,real_t _w2): x(_x),y(_y),z(_z),w0(_w0),w1(_w1),w2(_w2){ }
};


void ZBufferEngine::rasterizeMT(const Index4& rect,
                              const TOOLS(Vector3)& v0Raster, const TOOLS(Vector3)& v1Raster, const TOOLS(Vector3)& v2Raster, bool ccw, 
                              const TriangleShaderPtr& shader, const ProjectionCameraPtr& camera)
{
    rasterize(rect[0],rect[1],rect[2],rect[3],v0Raster,v1Raster,v2Raster,ccw,shader,camera);
    // printf("- rendering nb task : %u\n", uint32_t(__nb_tasks));
    threadmanager().end_task();    
}

void ZBufferEngine::rasterize(int32_t x0, int32_t x1, int32_t y0, int32_t y1,
                              TOOLS(Vector3) v0Raster, TOOLS(Vector3) v1Raster, TOOLS(Vector3) v2Raster, bool ccw, 
                              const TriangleShaderPtr& shader, const ProjectionCameraPtr& camera)
{


    real_t z0 = v0Raster.z();
    real_t z1 = v1Raster.z();
    real_t z2 = v2Raster.z();

    // Precompute reciprocal of vertex z-coordinate
    v0Raster.z() = 1. / v0Raster.z();
    v1Raster.z() = 1. / v1Raster.z();
    v2Raster.z() = 1. / v2Raster.z();

    real_t area = edgeFunction(v0Raster, v1Raster, v2Raster, ccw);

    std::queue<Fragment> fragqueue;

    // Inner loop
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {

            Vector2 pixelSample(x + 0.5, y + 0.5);

            // find weight of pixel
            real_t w0 = edgeFunction(v1Raster, v2Raster, pixelSample, ccw);
            real_t w1 = edgeFunction(v2Raster, v0Raster, pixelSample, ccw);
            real_t w2 = edgeFunction(v0Raster, v1Raster, pixelSample, ccw);

            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                w0 /= area;
                w1 /= area;
                w2 /= area;

                real_t oneOverZ = v0Raster.z() * w0 + v1Raster.z() * w1 + v2Raster.z() * w2;
                real_t z = 1. / oneOverZ;

                // Depth-buffer test
                if (camera->isInZRange(z)){

                    // Vec2f st = st0 * w0 + st1 * w1 + st2 * w2;                        
                    // st *= z;
                    if (isVisible(x, y, z)) {
                        if(tryLock(x,y)){
                            if (isVisible(x, y, z)) {
                                __depthBuffer->setAt(x, y, z);
                                if(is_valid_ptr(shader))shader->process(x, y, z, (w0 * z / z0), (w1 * z / z1), (w2 * z / z2));
                            }
                            unlock(x,y);
                        }
                        else {
                            fragqueue.push(Fragment(x,y,z,(w0 * z / z0), (w1 * z / z1), (w2 * z / z2)));

                        }
                    }                   
                }
            }
        }
    }
    while(!fragqueue.empty()) {
        Fragment f = fragqueue.front();
        fragqueue.pop();
        if(tryLock(f.x,f.y)){
            if (isVisible(f.x, f.y, f.z)) {
                __depthBuffer->setAt(f.x, f.y, f.z);
                if(is_valid_ptr(shader))shader->process(f.x, f.y, f.z, f.w0, f.w1, f.w2);
            }
            unlock(f.x,f.y);
        }
        else {
            fragqueue.push(f);
        }        
    }

}



void ZBufferEngine::renderTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, 
                                     const Color4& c0,  const Color4& c1,  const Color4& c2, 
                                     bool ccw, ProjectionCameraPtr camera)
{
    if(is_null_ptr(camera)) camera = __camera;
    ColorBasedShader * shader = new ColorBasedShader(this);
    shader->setColors(c0, c1, c2);
    renderShadedTriangle(v0, v1, v2, ccw, shader, camera);
}


void ZBufferEngine::renderPoint(const TOOLS(Vector3)& v, const Color4& c, const uint32_t width, ProjectionCameraPtr camera)
{
    
    if(is_null_ptr(camera)) camera = __camera;

    Vector3 vCam = camera->worldToCamera(v);

    // Convert the vertices of the triangle to raster space
    Vector3 vRaster = camera->cameraToRaster(vCam,__imageWidth, __imageHeight);

    // v0Raster.z() = 1. / v0Raster.z();

    // the point is out of screen
    if (vRaster.x() >= __imageWidth  || vRaster.x() < 0 || 
        vRaster.y() >= __imageHeight || vRaster.y() < 0 || 
        !camera->isInZRange(vRaster.z())) {
        return;
    }

    int32_t halfwidth = width/2.;
    int32_t x0 = pglMax<int32_t>(int32_t(0), (int32_t)(vRaster.x()) - halfwidth );
    int32_t x1 = pglMin<int32_t>(int32_t(__imageWidth) - 1, (int32_t)(vRaster.x()) + halfwidth );
    int32_t y0 = pglMax<int32_t>(int32_t(0), (int32_t)(vRaster.y()) - halfwidth );
    int32_t y1 = pglMin<int32_t>(int32_t(__imageHeight) - 1, (int32_t)(vRaster.y()) + halfwidth );

    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            renderRaster(x, y, vRaster.z(), c);
        }
    }
}

void ZBufferEngine::renderSegment(const TOOLS(Vector3)& v0, const TOOLS(Vector3)& v1, const Color4& c0, const Color4& c1, const uint32_t width, ProjectionCameraPtr camera)
{
    if(is_null_ptr(camera)) camera = __camera;

}


void ZBufferEngine::iprocess(PolylinePtr polyline, MaterialPtr material, uint32_t id, ProjectionCameraPtr camera, uint32_t threadid)
{
    if(is_null_ptr(camera)) camera = __camera;

}

void ZBufferEngine::iprocess(PointSetPtr pointset, MaterialPtr material, uint32_t id, ProjectionCameraPtr camera, uint32_t threadid)
{
    if(is_null_ptr(camera)) camera = __camera;

    const Point3ArrayPtr points(pointset->getPointList());
    Color4 defaultcolor = Color4(material->getAmbient(),material->getTransparency());
    Color4Array::const_iterator itCol;
    bool colorPerPoint = pointset->hasColorList();
    if (colorPerPoint) {
        itCol = pointset->getColorList()->begin();
    }
    uint32_t pointsize = pointset->getWidth();
    for (Point3Array::const_iterator it = points->begin(); it!= points->end(); ++it)
    {
        renderPoint(*it, (colorPerPoint?*itCol:defaultcolor), pointsize, camera);
    }
}

ImagePtr ZBufferEngine::getTexture(const ImageTexturePtr imgdef)
{
    Cache<ImagePtr>::const_Iterator it = __cachetexture.find(imgdef->getId());
    if (it != __cachetexture.end()){
        return it->second;
    }
    else {
        ImagePtr img(new Image(imgdef->getFilename()));
        __cachetexture.insert(imgdef->getId(),img);
        return img;
    }
}

void ZBufferEngine::process(ScenePtr scene)
{
    beginProcess();
    size_t msize = scene->size();
    if(__multithreaded && msize > 100){
        size_t nbthreads = threadmanager().nb_threads();
        size_t nbShapePerThread = (msize / nbthreads);
        if (nbShapePerThread * nbthreads < msize) { nbShapePerThread += 1; }

        if(__triangleshaderset != NULL) delete [] __triangleshaderset;
        __triangleshaderset = new TriangleShaderPtr[nbthreads];
        for (size_t j = 0 ; j < nbthreads ; ++j){
            __triangleshaderset[j] = TriangleShaderPtr(__triangleshader->copy(true));
        }


        uint32_t threadid = 1;
        for (size_t i = 0 ; i < msize ; i+=nbShapePerThread, ++threadid) {
            
            Scene::const_iterator itbegin = scene->begin() + i ;
            Scene::const_iterator itend = scene->begin() + pglMin(msize, i + nbShapePerThread);

            threadmanager().new_task(boost::bind(&ZBufferEngine::processSceneMT, this, itbegin, itend, ProjectionCameraPtr(__camera->copy()),threadid));

            
        }
    } 
    else {
        processScene(scene->begin(), scene->end(), __camera);
    }
    endProcess();
}

void ZBufferEngine::processSceneMT(Scene::const_iterator scene_begin, Scene::const_iterator scene_end, ProjectionCameraPtr camera, uint32_t threadid)
{
    processScene(scene_begin, scene_end, camera, threadid);
    threadmanager().end_task();
}

void ZBufferEngine::processScene(Scene::const_iterator scene_begin, Scene::const_iterator scene_end, ProjectionCameraPtr camera, uint32_t threadid)
{
    Discretizer d;
    Tesselator t;
    ProjectionRenderer r(*this, camera, t, d, threadid);
    for (Scene::const_iterator it = scene_begin; it != scene_end; ++it)
        (*it)->apply(r);
}


void ZBufferEngine::duplicateBuffer(const TOOLS(Vector3)& from, const TOOLS(Vector3)& to, bool useDefaultColor, const Color3& defaultcolor)
{
    Vector3 vRasterFrom = __camera->worldToRaster(from,__imageWidth, __imageHeight);
    Vector3 vRasterTo = __camera->worldToRaster(to,__imageWidth, __imageHeight);
    Vector3 vDiff = vRasterTo - vRasterFrom;

    duplicateBuffer(vDiff.x(), vDiff.y(), vDiff.z(), useDefaultColor, defaultcolor);
}

void ZBufferEngine::duplicateBuffer(int32_t xDiff, int32_t yDiff, real_t zDiff, bool useDefaultColor, const Color3& defaultcolor){
    if(xDiff == 0  && yDiff == 0 && fabs(zDiff) < GEOM_EPSILON) return;

    uint32_t width = __imageWidth;
    uint32_t height = __imageHeight;

    uint32_t xStart = 0;
    uint32_t xEnd = width;

    if (xDiff > 0) { 
        if (xDiff > width) { return; }
        xStart += xDiff;
    }
    else {
        if (-xDiff > width) { return; }
        xEnd += xDiff;

    }

    uint32_t yStart = 0;
    uint32_t yEnd = height;
    if (yDiff > 0) { 
        if (yDiff > height) { return; }
        yStart += yDiff;
    }
    else {
        if (-yDiff > height) { return; }
        yEnd += yDiff;
    }

    RealArray2Ptr depthBuffer(new RealArray2(*__depthBuffer));
    FrameBufferManagerPtr framebuffer = (is_valid_ptr(__frameBuffer) ? __frameBuffer->deepcopy() : NULL);

    for (uint32_t i = xStart ; i < xEnd ; ++i) {
        for (uint32_t j = yStart ; j < yEnd ; ++j) {
            real_t potentialz = depthBuffer->getAt(i-xDiff,j-yDiff)+zDiff;
            if (__camera->isInZRange(potentialz))
                renderRaster(i, j, potentialz, useDefaultColor || is_null_ptr(framebuffer) ? defaultcolor : framebuffer->getPixelAt(i-xDiff,j-yDiff));
        }
    }

}

void ZBufferEngine::periodizeBuffer(const TOOLS(Vector3)& from, const TOOLS(Vector3)& to, bool useDefaultColor, const Color3& defaultcolor)
{
    Vector3 vRasterFrom = __camera->worldToRaster(from,__imageWidth, __imageHeight);
    Vector3 vRasterTo = __camera->worldToRaster(to,__imageWidth, __imageHeight);
    Vector3 vDiff = vRasterTo - vRasterFrom;

    periodizeBuffer(vDiff.x(), vDiff.y(), vDiff.z(), useDefaultColor, defaultcolor);
}

void ZBufferEngine::periodizeBuffer(int32_t xDiff, int32_t yDiff, real_t zDiff, bool useDefaultColor, const Color3& defaultcolor)
{
    _bufferPeriodizationStep(xDiff, yDiff, zDiff, useDefaultColor, defaultcolor);
    _bufferPeriodizationStep(-xDiff, -yDiff, -zDiff, useDefaultColor, defaultcolor);

}

void ZBufferEngine::_bufferPeriodizationStep(int32_t xDiff, int32_t yDiff, real_t zDiff, bool useDefaultColor, const Color3& defaultcolor)
{
    if(xDiff == 0  && yDiff == 0 && fabs(zDiff) < GEOM_EPSILON) return;

    uint32_t width = __imageWidth;
    uint32_t height = __imageHeight;

    int32_t xStart = 0;
    int32_t xEnd = width;
    int32_t xStep = 1;

    if (fabs(xDiff) >= width) { return; }
    if (xDiff < 0) {
         xStart = width-1;
         xEnd = -1;
         xStep = -1;
    }
    xStart += xDiff;
    
    int32_t yStart = 0;
    int32_t yEnd = height;
    int32_t yStep = 1;

    if (fabs(yDiff) >= height) { return; }
    /*if (yDiff < 0) {
         yStart = height-1;
         yEnd = -1;
         yStep = -1;
    }*/
    if (yDiff < 0) {
        yEnd += yDiff;
    }
    else {
        yStart += yDiff;        
    }

    // printf("%i %i %f\n", xDiff, yDiff, zDiff);
    // printf("x:%i %i %i\n", xStart, xEnd, xStep);
    // printf("y:%i %i %i\n", yStart, yEnd, yStep);

    for (int32_t i = xStart ; i != xEnd ; i+=xStep) {
        for (int32_t j = yStart ; j < yEnd ; j+=yStep) {
            real_t potentialz = __depthBuffer->getAt(i-xDiff,j-yDiff)+zDiff;
            // if (__camera->isInZRange(potentialz))
                renderRaster(i, j, potentialz, useDefaultColor || is_null_ptr(__frameBuffer)? defaultcolor : getFrameBufferAt(i-xDiff,j-yDiff));
        }
    }
}


boost::asio::thread_pool * 
ZBufferEngine::ZbufferEngineThreadManager::getPool()
{
    if(__pool == NULL) __pool = new boost::asio::thread_pool(__nb_threads);
    return __pool;
}

ImageMutexPtr ZBufferEngine::ZbufferEngineThreadManager::getImageMutex(uint16_t imageWidth, uint16_t imageHeight)
{
    if (is_null_ptr(__imageMutex) ||  imageWidth > __imageMutex->width() || imageHeight > __imageMutex->height()){
        __imageMutex = ImageMutexPtr(new ImageMutex(uint_t(imageWidth), uint_t(imageHeight)));
    }
   return __imageMutex;
}

ZBufferEngine::ZbufferEngineThreadManager::ZbufferEngineThreadManager(): 
    __pool(NULL), __imageMutex(),
    __condition_lock(__condition_mutex),
    __nb_threads(boost::thread::hardware_concurrency()+1)
{}

ZBufferEngine::ZbufferEngineThreadManager::~ZbufferEngineThreadManager()
{
    if(__pool != NULL) delete __pool;
}

void ZBufferEngine::ZbufferEngineThreadManager::init_tasks()
{ 
    getPool();
    __nb_tasks = 0;
}

template<typename Task> 
void process_task(Task&& task){
    task();
    ZBufferEngine::threadmanager().end_task();
};

template<typename Task> 
void ZBufferEngine::ZbufferEngineThreadManager::new_task(Task&& task){
    ++__nb_tasks;
    // boost::asio::post(*__pool, boost::bind(&process_task<Task>,task));    
    boost::asio::post(*__pool, task);    
}

void ZBufferEngine::ZbufferEngineThreadManager::end_task()
{
    --__nb_tasks;
    __condition.notify_one();
}

void ZBufferEngine::ZbufferEngineThreadManager::join()
{
    __condition.wait(__condition_lock, boost::bind(&ZBufferEngine::ZbufferEngineThreadManager::hasCompletedTasks, this));
}


bool ZBufferEngine::ZbufferEngineThreadManager::hasCompletedTasks() const { 
    // printf("rendering nb task : %u\n", uint32_t(__nb_tasks));
    return __nb_tasks == 0; 
}



// Singleton access
ZBufferEngine::ZbufferEngineThreadManager& ZBufferEngine::threadmanager() { return ZBufferEngine::THREADMANAGER; }

ZBufferEngine::ZbufferEngineThreadManager ZBufferEngine::THREADMANAGER;
