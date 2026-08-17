/**
 * @file SampleScene.h
 * @brief 一张开箱即有的图纸 —— 打开程序不至于对着空场景发呆。
 */
#ifndef CADGEOM_MFC_VIEWER_SAMPLESCENE_H
#define CADGEOM_MFC_VIEWER_SAMPLESCENE_H

#include <cadgeom/CadGeom.h>

/// @brief 画一块安装板：外框、中心孔、节圆上的四个螺栓孔、中心线，外加一个由圆
///        拉伸出来的凸台。
/// @param scene 目标场景；已有内容不会被清掉，调用方自己决定要不要先 Clear()。
/// @note 每一样都走 IGeometryBuilder，因此整张图纸是一串可撤销的命令；用一个
///       UndoGroup 收成一步，Ctrl+Z 一次就干净了。
void BuildSampleScene(cadgeom::IScene& scene);

#endif // CADGEOM_MFC_VIEWER_SAMPLESCENE_H
