#include <yoga/Yoga.h>
#include <algorithm>
#include <cmath>
#include <new>
#include <vector>

// Replay Yoga with answers supplied between passes. No managed pointers or
// interpreter callbacks cross this API. Queries are scoped to one layout.
struct Node {
  YGNodeRef yoga;
  struct Query { float w, h; YGMeasureMode wm, hm; YGSize size; bool answered; };
  std::vector<Query> queries;
  bool overflow = false;
};
static int live_nodes = 0;
static YGConfigRef config = nullptr;
static YGSize measure(YGNodeConstRef yoga, float width, YGMeasureMode wm,
                      float height, YGMeasureMode hm) {
  auto* n = static_cast<Node*>(YGNodeGetContext(yoga));
  for (int i = 0; i < static_cast<int>(n->queries.size()); ++i) {
    auto& q = n->queries[i];
    if (q.wm == wm && q.hm == hm && (wm == YGMeasureModeUndefined || q.w == width) &&
        (hm == YGMeasureModeUndefined || q.h == height)) {
      return q.size;
    }
  }
  YGSize placeholder = {wm == YGMeasureModeExactly ? width : 0,
                         hm == YGMeasureModeExactly ? height : 0};
  if (n->queries.size() == 256) { n->overflow = true; return placeholder; }
  try { n->queries.push_back({width, height, wm, hm, placeholder, false}); }
  catch (const std::bad_alloc&) { n->overflow = true; }
  return placeholder;
}
extern "C" {
void otui_text_clear();
int otui_live_renderers();
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
  if (--live_nodes == 0) {
    YGConfigFree(config); config = nullptr;
    if (!otui_live_renderers()) otui_text_clear();
  }
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
bool otui_node_padding(Node* n, float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0) return false;
  YGNodeStyleSetPadding(n->yoga, YGEdgeHorizontal, x);
  YGNodeStyleSetPadding(n->yoga, YGEdgeVertical, y);
  return true;
}
// Cross-axis alignment of children: 0 stretch, 1 start, 2 center, 3 end.
void otui_node_align(Node* n, int align) {
  static const YGAlign modes[] = {YGAlignStretch, YGAlignFlexStart, YGAlignCenter, YGAlignFlexEnd};
  YGNodeStyleSetAlignItems(n->yoga, modes[align >= 0 && align < 4 ? align : 0]);
}
void otui_node_center(Node* n, bool center) {
  YGNodeStyleSetJustifyContent(n->yoga, center ? YGJustifyCenter : YGJustifyFlexStart);
}
void otui_node_scroll(Node* n, bool scroll) {
  YGNodeStyleSetOverflow(n->yoga, scroll ? YGOverflowScroll : YGOverflowVisible);
  if (scroll && YGNodeStyleGetFlexGrow(n->yoga) > 0) YGNodeStyleSetFlexBasis(n->yoga, 0);
  else YGNodeStyleSetFlexBasisAuto(n->yoga);
}
void otui_node_shrink(Node* n, bool shrink) {
  YGNodeStyleSetFlexShrink(n->yoga, shrink ? 1 : 0);
}
bool otui_node_intrinsic(Node* n, float width, float height, bool enabled) {
  if (!std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0) return false;
  n->queries.clear(); n->overflow = false;
  YGNodeSetMeasureFunc(n->yoga, enabled ? measure : nullptr);
  if (enabled) YGNodeMarkDirty(n->yoga);
  return true;
}
int otui_node_pending(Node* n) {
  if (n->overflow) return -2;
  for (int i = 0; i < static_cast<int>(n->queries.size()); ++i) if (!n->queries[i].answered) return i;
  return -1;
}
float otui_node_query(Node* n, int i, int field) {
  auto& q = n->queries[i];
  switch (field) { case 0: return q.w; case 1: return q.h;
    case 2: return q.wm; default: return q.hm; }
}
bool otui_node_answer(Node* n, int i, float w, float h) {
  if (!std::isfinite(w) || !std::isfinite(h) || w < 0 || h < 0) return false;
  auto& q = n->queries[i];
  auto fit = [](float v, float a, YGMeasureMode m) {
    return m == YGMeasureModeExactly ? a : m == YGMeasureModeAtMost ? std::min(v,a) : v;
  };
  q.size = {fit(w,q.w,q.wm), fit(h,q.h,q.hm)}; q.answered = true;
  return true;
}
void otui_node_retry(Node* n) {
  if (YGNodeHasMeasureFunc(n->yoga)) YGNodeMarkDirty(n->yoga);
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
