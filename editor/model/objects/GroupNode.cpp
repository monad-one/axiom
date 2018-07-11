#include "GroupNode.h"

#include "../ModelRoot.h"
#include "../PoolOperators.h"
#include "../ReferenceMapper.h"
#include "ControlSurface.h"
#include "editor/compiler/interface/Runtime.h"

using namespace AxiomModel;

GroupNode::GroupNode(const QUuid &uuid, const QUuid &parentUuid, QPoint pos, QSize size, bool selected, QString name,
                     const QUuid &controlsUuid, const QUuid &innerUuid, AxiomModel::ModelRoot *root)
    : Node(NodeType::GROUP_NODE, uuid, parentUuid, pos, size, selected, std::move(name), controlsUuid, root),
      _nodes(findLater<GroupSurface *>(root->nodeSurfaces(), innerUuid)) {}

std::unique_ptr<GroupNode> GroupNode::create(const QUuid &uuid, const QUuid &parentUuid, QPoint pos, QSize size,
                                             bool selected, QString name, const QUuid &controlsUuid,
                                             const QUuid &innerUuid, AxiomModel::ModelRoot *root) {
    return std::make_unique<GroupNode>(uuid, parentUuid, pos, size, selected, name, controlsUuid, innerUuid, root);
}

std::unique_ptr<GroupNode> GroupNode::deserialize(QDataStream &stream, const QUuid &uuid, const QUuid &parentUuid,
                                                  QPoint pos, QSize size, bool selected, QString name,
                                                  const QUuid &controlsUuid, ReferenceMapper *ref,
                                                  AxiomModel::ModelRoot *root) {
    QUuid innerUuid;
    stream >> innerUuid;
    innerUuid = ref->mapUuid(innerUuid);

    return create(uuid, parentUuid, pos, size, selected, std::move(name), controlsUuid, innerUuid, root);
}

void GroupNode::serialize(QDataStream &stream, const QUuid &parent, bool withContext) const {
    Node::serialize(stream, parent, withContext);
    stream << (*_nodes.value())->uuid();
}

void GroupNode::attachRuntime(MaximCompiler::Runtime *runtime, MaximCompiler::Transaction *transaction) {
    nodes().then([runtime, transaction](NodeSurface *const &surface) { surface->attachRuntime(runtime, transaction); });
}

void GroupNode::updateRuntimePointers(MaximCompiler::Runtime *runtime, void *surfacePtr) {
    Node::updateRuntimePointers(runtime, surfacePtr);

    auto nodePtr = runtime->getNodePtr(surface()->getRuntimeId(), surfacePtr, compileMeta()->mirIndex);
    auto subsurfacePtr = runtime->getSurfacePtr(nodePtr);
    nodes().then([subsurfacePtr, runtime](GroupSurface *subsurface) {
        subsurface->updateRuntimePointers(runtime, subsurfacePtr);
    });
}

void GroupNode::remove() {
    if (_nodes.value()) (*_nodes.value())->remove();
    Node::remove();
}
