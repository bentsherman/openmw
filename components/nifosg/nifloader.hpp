#ifndef OPENMW_COMPONENTS_NIFOSG_LOADER
#define OPENMW_COMPONENTS_NIFOSG_LOADER

#include <components/nif/niffile.hpp>

#include <components/nifcache/nifcache.hpp> // NIFFilePtr

#include <components/vfs/manager.hpp>

#include <osg/Group>

namespace osg
{
    class Geometry;
}
namespace osgAnimation
{
    class Bone;
}

namespace Nif
{
    class Node;
    class NiTriShape;
    class Property;
    class Controller;
}

namespace NifOsg
{
    class Controller;

    /// The main class responsible for loading NIF files into an OSG-Scenegraph.
    class Loader
    {
    public:
        /// @param node The parent of the root node for the created NIF file.
        void load(Nif::NIFFilePtr file, osg::Group* parentNode);

        void loadAsSkeleton(Nif::NIFFilePtr file, osg::Group* parentNode);

        const VFS::Manager* resourceManager;

    private:

        /// @param createSkeleton If true, use an osgAnimation::Bone for NIF nodes, otherwise an osg::MatrixTransform.
        void handleNode(const Nif::Node* nifNode, osg::Group* parentNode, bool createSkeleton,
                        std::map<int, int> boundTextures, int animflags, int particleflags, bool collisionNode=false);

        void handleMeshControllers(const Nif::Node* nifNode, osg::MatrixTransform* transformNode, const std::map<int, int>& boundTextures, int animflags);

        void handleNodeControllers(const Nif::Node* nifNode, osg::MatrixTransform* transformNode, int animflags);

        void handleMaterialControllers(const Nif::Property* materialProperty, osg::Node* node, osg::StateSet* stateset, int animflags);

        void handleTextureControllers(const Nif::Property* texProperty, osg::Node* node, osg::StateSet* stateset, int animflags);

        void handleProperty (const Nif::Property* property, const Nif::Node* nifNode,
                             osg::Node* node, std::map<int, int>& boundTextures, int animflags);

        void handleParticleSystem(const Nif::Node* nifNode, osg::Group* parentNode, int animflags, int particleflags);

        // Creates an osg::Geometry object for the given TriShape, populates it, and attaches it to the given node.
        void handleTriShape(const Nif::NiTriShape* triShape, osg::Group* parentNode, const std::map<int, int>& boundTextures, int animflags);

        // Fills the vertex data for the given TriShape into the given Geometry.
        void triShapeToGeometry(const Nif::NiTriShape* triShape, osg::Geometry* geom, osg::Geode* parentGeode, const std::map<int, int>& boundTextures, int animflags);

        // Creates a skinned osg::Geometry object for the given TriShape, populates it, and attaches it to the given node.
        void handleSkinnedTriShape(const Nif::NiTriShape* triShape, osg::Group* parentNode, const std::map<int, int>& boundTextures, int animflags);

        // Applies the Properties of the given nifNode onto the StateSet of the given OSG node.
        void applyNodeProperties(const Nif::Node* nifNode, osg::Node* applyTo, std::map<int, int>& boundTextures, int animflags);

        void applyMaterialProperties(osg::Node* node, const std::vector<const Nif::Property*>& properties,
                                     bool hasVertexColors, int animflags);

        // Set up the default input and controller function for the given controller.
        void setupController(const Nif::Controller* ctrl, Controller* toSetup, int animflags);

        Nif::NIFFilePtr mNif;

        osg::Group* mRootNode;
        osg::Group* mSkeleton;
    };

}

#endif