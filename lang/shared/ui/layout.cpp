#include <yoga/Yoga.h>
#include <algorithm>
#include <cmath>
#include <new>

// Native nodes contain only native data. Otium measures intrinsic leaf sizes
// before Yoga runs; no managed pointers or interpreter callbacks cross this API.
struct Node {
  YGNodeRef yoga;
  float width = 0, height = 0;
};
static int live_nodes = 0;
static YGConfigRef config = nullptr;
static YGSize measure(YGNodeConstRef yoga, float width, YGMeasureMode wm,
                      float height, YGMeasureMode hm) {
  auto* n = static_cast<Node*>(YGNodeGetContext(yoga));
  auto fit = [](float preferred, float available, YGMeasureMode mode) {
    return mode == YGMeasureModeExactly ? available :
        mode == YGMeasureModeAtMost ? std::min(preferred, available) : preferred;
  };
  return {fit(n->width, width, wm), fit(n->height, height, hm)};
}
extern "C" {
void* otui_node_new() {
  auto* n = new (std::nothrow) Node;
  if (!n) return nullptr;
  if (!config) {
    config = YGConfigNew();
    YGConfigSetPointScaleFactor(config, 0); // Keep logical layout fractional.
  }
  n->yoga = YGNodeNewWithConfig(config);
  YGNodeSetContext(n->yoga, n);
  ++live_nodes;
  return n;
}
void otui_node_free(Node* n) {
  if (!n) return;
  YGNodeFree(n->yoga);
  delete n;
  if (--live_nodes == 0) { YGConfigFree(config); config = nullptr; }
}
int otui_live_nodes() { return live_nodes; }
void otui_node_clear(Node* n) { YGNodeRemoveAllChildren(n->yoga); }
void otui_node_append(Node* parent, Node* child) {
  YGNodeInsertChild(parent->yoga, child->yoga, YGNodeGetChildCount(parent->yoga));
}
bool otui_node_style(Node* n, float width, float height, float grow,
                     float padding, float gap, int row) {
  if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(grow) ||
      !std::isfinite(padding) || !std::isfinite(gap) || width < -1 || height < -1 ||
      grow < 0 || padding < 0 || gap < 0) return false;
  if (width < 0) YGNodeStyleSetWidthAuto(n->yoga); else YGNodeStyleSetWidth(n->yoga, width);
  if (height < 0) YGNodeStyleSetHeightAuto(n->yoga); else YGNodeStyleSetHeight(n->yoga, height);
  YGNodeStyleSetFlexDirection(n->yoga, row ? YGFlexDirectionRow : YGFlexDirectionColumn);
  YGNodeStyleSetFlexGrow(n->yoga, grow);
  YGNodeStyleSetFlexShrink(n->yoga, 1);
  YGNodeStyleSetPadding(n->yoga, YGEdgeAll, padding);
  YGNodeStyleSetGap(n->yoga, YGGutterAll, gap);
  return true;
}
bool otui_node_intrinsic(Node* n, float width, float height, bool enabled) {
  if (!std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0) return false;
  n->width = width;
  n->height = height;
  YGNodeSetMeasureFunc(n->yoga, enabled ? measure : nullptr);
  if (enabled) YGNodeMarkDirty(n->yoga);
  return true;
}
void otui_node_layout(Node* n, float width, float height) {
  YGNodeCalculateLayout(n->yoga, width, height, YGDirectionLTR);
}
float otui_node_bounds(Node* n, int field) {
  switch (field) {
    case 0: return YGNodeLayoutGetLeft(n->yoga);
    case 1: return YGNodeLayoutGetTop(n->yoga);
    case 2: return YGNodeLayoutGetWidth(n->yoga);
    case 3: return YGNodeLayoutGetHeight(n->yoga);
    default: return 0;
  }
}
}
