#include "myguirendermanager.hpp"

#include <stdexcept>

#include <MyGUI_Gui.h>
#include <MyGUI_Timer.h>

#include <osg/Drawable>
#include <osg/Geode>
#include <osg/BlendFunc>
#include <osg/Texture2D>

#include <osgViewer/Viewer>

#include <osgGA/GUIEventHandler>

#include <components/resource/texturemanager.hpp>

#include "myguitexture.hpp"

#define MYGUI_PLATFORM_LOG_SECTION "Platform"
#define MYGUI_PLATFORM_LOG(level, text) MYGUI_LOGGING(MYGUI_PLATFORM_LOG_SECTION, level, text)

#define MYGUI_PLATFORM_EXCEPT(dest) do { \
    MYGUI_PLATFORM_LOG(Critical, dest); \
    MYGUI_DBG_BREAK;\
    std::ostringstream stream; \
    stream << dest << "\n"; \
    MYGUI_BASE_EXCEPT(stream.str().c_str(), "MyGUI"); \
} while(0)

#define MYGUI_PLATFORM_ASSERT(exp, dest) do { \
    if ( ! (exp) ) \
    { \
        MYGUI_PLATFORM_LOG(Critical, dest); \
        MYGUI_DBG_BREAK;\
        std::ostringstream stream; \
        stream << dest << "\n"; \
        MYGUI_BASE_EXCEPT(stream.str().c_str(), "MyGUI"); \
    } \
} while(0)

namespace
{

// Proxy to forward an OSG resize event to RenderManager::setViewSize
class ResizeHandler : public osgGA::GUIEventHandler {
    osgMyGUI::RenderManager *mParent;

    virtual bool handle(const osgGA::GUIEventAdapter &ea, osgGA::GUIActionAdapter &aa)
    {
        if(ea.getEventType() == osgGA::GUIEventAdapter::RESIZE)
        {
            int width = ea.getWindowWidth();
            int height = ea.getWindowHeight();
            mParent->setViewSize(width, height);
        }
        return false;
    }

public:
    ResizeHandler(osgMyGUI::RenderManager *parent=nullptr) : mParent(parent) { }
    ResizeHandler(const ResizeHandler &copy, const osg::CopyOp &copyop=osg::CopyOp::SHALLOW_COPY)
        : osg::Object(copy, copyop), osgGA::GUIEventHandler(copy, copyop)
        , mParent(copy.mParent)
    { }

    META_Object(osgMyGUI, ResizeHandler)
};

}


namespace osgMyGUI
{

class Drawable : public osg::Drawable {
    osgMyGUI::RenderManager *mParent;

    // Stage 0: update widget animations and controllers. Run during the Update traversal.
    class FrameUpdate : public osg::Drawable::UpdateCallback
    {
    public:
        FrameUpdate()
            : mRenderManager(NULL)
        {
        }

        void setRenderManager(osgMyGUI::RenderManager* renderManager)
        {
            mRenderManager = renderManager;
        }

        virtual void update(osg::NodeVisitor*, osg::Drawable*)
        {
            if (mRenderManager)
                mRenderManager->update();
        }

    private:
        osgMyGUI::RenderManager* mRenderManager;
    };

    // Stage 1: collect draw calls. Run during the Cull traversal.
    class CollectDrawCalls : public osg::Drawable::CullCallback
    {
    public:
        CollectDrawCalls()
            : mRenderManager(NULL)
        {
        }

        void setRenderManager(osgMyGUI::RenderManager* renderManager)
        {
            mRenderManager = renderManager;
        }

        virtual bool cull(osg::NodeVisitor*, osg::Drawable*, osg::State*) const
        {
            if (!mRenderManager)
                return false;

            mRenderManager->collectDrawCalls();
            return false;
        }

    private:
        osgMyGUI::RenderManager* mRenderManager;
    };

    // Stage 2: execute the draw calls. Run during the Draw traversal. May run in parallel with the update traversal of the next frame.
    virtual void drawImplementation(osg::RenderInfo &renderInfo) const
    {
        osg::State *state = renderInfo.getState();
        state->disableAllVertexArrays();
        state->setClientActiveTextureUnit(0);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);

        mReadFrom = (mReadFrom+1)%2;
        const std::vector<Batch>& vec = mBatchVector[mReadFrom];
        for (std::vector<Batch>::const_iterator it = vec.begin(); it != vec.end(); ++it)
        {
            const Batch& batch = *it;
            osg::VertexBufferObject *vbo = batch.mVertexBuffer;
            osg::Texture2D* texture = batch.mTexture;
            if(texture)
                state->applyTextureAttribute(0, texture);

            osg::GLBufferObject* bufferobject = 0;//state->isVertexBufferObjectSupported() ? vbo->getOrCreateGLBufferObject(state->getContextID()) : 0;
            if (bufferobject)
            {
                state->bindVertexBufferObject(bufferobject);

                glVertexPointer(3, GL_FLOAT, sizeof(MyGUI::Vertex), (char*)NULL);
                glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(MyGUI::Vertex), (char*)NULL + 12);
                glTexCoordPointer(2, GL_FLOAT, sizeof(MyGUI::Vertex), (char*)NULL + 16);
            }
            else
            {
                glVertexPointer(3, GL_FLOAT, sizeof(MyGUI::Vertex), (char*)vbo->getArray(0)->getDataPointer());
                glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(MyGUI::Vertex), (char*)vbo->getArray(0)->getDataPointer() + 12);
                glTexCoordPointer(2, GL_FLOAT, sizeof(MyGUI::Vertex), (char*)vbo->getArray(0)->getDataPointer() + 16);
            }

            glDrawArrays(GL_TRIANGLES, 0, batch.mVertexCount);
        }

        state->unbindVertexBufferObject();
        state->dirtyAllVertexArrays();
        state->disableAllVertexArrays();
    }

public:
    Drawable(osgMyGUI::RenderManager *parent = nullptr)
        : mParent(parent)
        , mWriteTo(0)
        , mReadFrom(0)
    {
        setSupportsDisplayList(false);

        osg::ref_ptr<CollectDrawCalls> collectDrawCalls = new CollectDrawCalls;
        collectDrawCalls->setRenderManager(mParent);
        setCullCallback(collectDrawCalls);

        osg::ref_ptr<FrameUpdate> frameUpdate = new FrameUpdate;
        frameUpdate->setRenderManager(mParent);
        setUpdateCallback(frameUpdate);
    }
    Drawable(const Drawable &copy, const osg::CopyOp &copyop=osg::CopyOp::SHALLOW_COPY)
        : osg::Drawable(copy, copyop)
        , mParent(copy.mParent)
    {
    }

    // Defines the necessary information for a draw call
    struct Batch
    {
        // May be empty
        osg::ref_ptr<osg::Texture2D> mTexture;

        osg::ref_ptr<osg::VertexBufferObject> mVertexBuffer;
        // need to hold on to this too as the mVertexBuffer does not hold a ref to its own array
        osg::ref_ptr<osg::UByteArray> mArray;

        size_t mVertexCount;
    };

    void addBatch(const Batch& batch)
    {
        mBatchVector[mWriteTo].push_back(batch);
    }

    void clear()
    {
        mWriteTo = (mWriteTo+1)%2;
        mBatchVector[mWriteTo].clear();
    }

    META_Object(osgMyGUI, Drawable)

private:
    std::vector<Batch> mBatchVector[2];
    int mWriteTo;
    mutable int mReadFrom;
};

class OSGVertexBuffer : public MyGUI::IVertexBuffer
{
    osg::ref_ptr<osg::VertexBufferObject> mBuffer;
    osg::ref_ptr<osg::UByteArray> mVertexArray;

    size_t mNeedVertexCount;

public:
    OSGVertexBuffer();
    virtual ~OSGVertexBuffer();

    virtual void setVertexCount(size_t count);
    virtual size_t getVertexCount();

    virtual MyGUI::Vertex *lock();
    virtual void unlock();

/*internal:*/
    void destroy();
    void create();

    osg::VertexBufferObject *getBuffer() const { return mBuffer.get(); }
    osg::UByteArray *getArray() const { return mVertexArray.get(); }
};

OSGVertexBuffer::OSGVertexBuffer()
  : mNeedVertexCount(0)
{
}

OSGVertexBuffer::~OSGVertexBuffer()
{
    destroy();
}

void OSGVertexBuffer::setVertexCount(size_t count)
{
    if(count == mNeedVertexCount)
        return;

    mNeedVertexCount = count;
    destroy();
    create();
}

size_t OSGVertexBuffer::getVertexCount()
{
    return mNeedVertexCount;
}

MyGUI::Vertex *OSGVertexBuffer::lock()
{
    // Force recreating the buffer, to make sure we are not modifying a buffer currently
    // queued for rendering in the last frame's draw thread.
    // a more efficient solution might be double buffering
    destroy();
    create();

    MYGUI_PLATFORM_ASSERT(mBuffer.valid(), "Vertex buffer is not created");

    mVertexArray->resize(mNeedVertexCount * sizeof(MyGUI::Vertex));
    return (MyGUI::Vertex*)&(*mVertexArray)[0];
}

void OSGVertexBuffer::unlock()
{
    mVertexArray->dirty();
    mBuffer->dirty();
}

void OSGVertexBuffer::destroy()
{
    mBuffer = nullptr;
    mVertexArray = nullptr;
}

void OSGVertexBuffer::create()
{
    MYGUI_PLATFORM_ASSERT(!mBuffer.valid(), "Vertex buffer already exist");

    mVertexArray = new osg::UByteArray(mNeedVertexCount*sizeof(MyGUI::Vertex));

    mBuffer = new osg::VertexBufferObject;
    mBuffer->setDataVariance(osg::Object::DYNAMIC);
    mBuffer->setUsage(GL_DYNAMIC_DRAW);
    // NB mBuffer does not own the array
    mBuffer->setArray(0, mVertexArray.get());
}

// ---------------------------------------------------------------------------

RenderManager::RenderManager(osgViewer::Viewer *viewer, osg::Group *sceneroot, Resource::TextureManager* textureManager)
  : mViewer(viewer)
  , mSceneRoot(sceneroot)
  , mTextureManager(textureManager)
  , mUpdate(false)
  , mIsInitialise(false)
{
}

RenderManager::~RenderManager()
{
    MYGUI_PLATFORM_LOG(Info, "* Shutdown: "<<getClassTypeName());

    if(mGuiRoot.valid())
        mSceneRoot->removeChild(mGuiRoot.get());
    mGuiRoot = nullptr;
    mSceneRoot = nullptr;
    mViewer = nullptr;

    destroyAllResources();

    MYGUI_PLATFORM_LOG(Info, getClassTypeName()<<" successfully shutdown");
    mIsInitialise = false;
}


void RenderManager::initialise()
{
    MYGUI_PLATFORM_ASSERT(!mIsInitialise, getClassTypeName()<<" initialised twice");
    MYGUI_PLATFORM_LOG(Info, "* Initialise: "<<getClassTypeName());

    mVertexFormat = MyGUI::VertexColourType::ColourABGR;

    mUpdate = false;

    mDrawable = new Drawable(this);

    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    geode->addDrawable(mDrawable.get());

    osg::ref_ptr<osg::Camera> camera = new osg::Camera();
    camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    camera->setProjectionResizePolicy(osg::Camera::FIXED);
    camera->setProjectionMatrix(osg::Matrix::identity());
    camera->setViewMatrix(osg::Matrix::identity());
    camera->setRenderOrder(osg::Camera::POST_RENDER);
    camera->setClearMask(GL_NONE);
    osg::StateSet *state = new osg::StateSet;
    state->setTextureMode(0, GL_TEXTURE_2D, osg::StateAttribute::ON);
    state->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    state->setMode(GL_BLEND, osg::StateAttribute::ON);
    state->setAttribute(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    geode->setStateSet(state);
    geode->setCullingActive(false);
    camera->addChild(geode.get());

    mGuiRoot = camera;
    mSceneRoot->addChild(mGuiRoot.get());
    mViewer->addEventHandler(new ResizeHandler(this));

    osg::ref_ptr<osg::Viewport> vp = mViewer->getCamera()->getViewport();
    setViewSize(vp->width(), vp->height());

    MYGUI_PLATFORM_LOG(Info, getClassTypeName()<<" successfully initialized");
    mIsInitialise = true;
}

void RenderManager::shutdown()
{
}

MyGUI::IVertexBuffer* RenderManager::createVertexBuffer()
{
    return new OSGVertexBuffer();
}

void RenderManager::destroyVertexBuffer(MyGUI::IVertexBuffer *buffer)
{
    delete buffer;
}


void RenderManager::begin()
{
}

void RenderManager::doRender(MyGUI::IVertexBuffer *buffer, MyGUI::ITexture *texture, size_t count)
{
    Drawable::Batch batch;
    batch.mVertexCount = count;
    batch.mVertexBuffer = static_cast<OSGVertexBuffer*>(buffer)->getBuffer();
    batch.mArray = static_cast<OSGVertexBuffer*>(buffer)->getArray();
    if (texture)
        batch.mTexture = static_cast<OSGTexture*>(texture)->getTexture();

    mDrawable->addBatch(batch);
}

void RenderManager::end()
{
}

void RenderManager::update()
{
    static MyGUI::Timer timer;
    static unsigned long last_time = timer.getMilliseconds();
    unsigned long now_time = timer.getMilliseconds();
    unsigned long time = now_time - last_time;

    onFrameEvent((float)((double)(time) / (double)1000));

    last_time = now_time;
}

void RenderManager::collectDrawCalls()
{
    mDrawable->clear();
    onRenderToTarget(this, mUpdate);

    mUpdate = false;
}

void RenderManager::setViewSize(int width, int height)
{
    if(width < 1) width = 1;
    if(height < 1) height = 1;

    mGuiRoot->setViewport(0, 0, width, height);
    mViewSize.set(width, height);

    mInfo.maximumDepth = 1;
    mInfo.hOffset = 0;
    mInfo.vOffset = 0;
    mInfo.aspectCoef = float(mViewSize.height) / float(mViewSize.width);
    mInfo.pixScaleX = 1.0f / float(mViewSize.width);
    mInfo.pixScaleY = 1.0f / float(mViewSize.height);

    onResizeView(mViewSize);
    mUpdate = true;
}


bool RenderManager::isFormatSupported(MyGUI::PixelFormat /*format*/, MyGUI::TextureUsage /*usage*/)
{
    return true;
}

MyGUI::ITexture* RenderManager::createTexture(const std::string &name)
{
    MapTexture::const_iterator item = mTextures.find(name);
    MYGUI_PLATFORM_ASSERT(item == mTextures.end(), "Texture '"<<name<<"' already exist");

    OSGTexture* texture = new OSGTexture(name, mTextureManager);
    mTextures.insert(std::make_pair(name, texture));
    return texture;
}

void RenderManager::destroyTexture(MyGUI::ITexture *texture)
{
    if(texture == nullptr)
        return;

    MapTexture::iterator item = mTextures.find(texture->getName());
    MYGUI_PLATFORM_ASSERT(item != mTextures.end(), "Texture '"<<texture->getName()<<"' not found");

    mTextures.erase(item);
    delete texture;
}

MyGUI::ITexture* RenderManager::getTexture(const std::string &name)
{
    if (name.empty())
        return NULL;

    MapTexture::const_iterator item = mTextures.find(name);
    if(item == mTextures.end())
    {
        MyGUI::ITexture* tex = createTexture(name);
        tex->loadFromFile(name);
        return tex;
    }
    return item->second;
}

void RenderManager::destroyAllResources()
{
    for (MapTexture::iterator it = mTextures.begin(); it != mTextures.end(); ++it)
        delete it->second;
    mTextures.clear();
}

bool RenderManager::checkTexture(MyGUI::ITexture* _texture)
{
    for (MapTexture::const_iterator item = mTextures.begin(); item != mTextures.end(); ++item)
    {
        if (item->second == _texture)
            return true;
    }
    return false;
}

}