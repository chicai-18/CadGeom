/**
 * @file resource.h
 * @brief 资源 id。手写的，不是 VS 资源编辑器生成的 —— 这份示例要能在没有 IDE 的
 *        情况下读懂和改动。
 *
 * 分段：
 *   128..           窗口与对话框模板
 *   1000..          控件
 *   32771..         命令（MFC 约定命令 id 从 0x8000=32768 起）
 *   59200..         状态栏窗格
 *
 * MFC 自带的标准命令直接用 afxres.h 里的那批（ID_FILE_NEW、ID_APP_EXIT、
 * ID_EDIT_UNDO …）：框架认识它们，工具栏和菜单的启停也就不用自己写。
 */
#ifndef CADGEOM_MFC_VIEWER_RESOURCE_H
#define CADGEOM_MFC_VIEWER_RESOURCE_H

// -- 窗口 / 对话框 ----------------------------------------------------------

#define IDR_MAINFRAME                   128
#define IDD_ABOUTBOX                    130
#define IDD_INPUT                       131
#define IDD_PROGRESS                    132
#define IDD_UNITS                       133
#define IDD_SHAPE_PARAMS                134
#define IDD_STYLE                       135
#define IDD_TRANSFORM                   136
#define IDD_CAMERA                      137
#define IDD_TESS                        138
#define IDD_SHORTCUTS                   139

// -- 控件 -------------------------------------------------------------------

#define IDC_INPUT_PROMPT                1001
#define IDC_INPUT_EDIT                  1002
#define IDC_PROGRESS_TEXT               1003
#define IDC_PROGRESS_BAR                1004
#define IDC_ABOUT_TEXT                  1005
#define IDC_SHORTCUT_TEXT               1006

#define IDC_UNIT_MODEL                  1010
#define IDC_UNIT_DISPLAY                1011
#define IDC_UNIT_ANGLE                  1012
#define IDC_UNIT_LINPREC                1013
#define IDC_UNIT_ANGPREC                1014
#define IDC_UNIT_SUFFIX                 1015

// 形状参数对话框：模板是固定的，用不到的行在 OnInitDialog 里藏起来并把下面的
// 行往上收（见 Dialogs.cpp 的注释）。三行向量 + 四个标量 + 两个复选框，够覆盖
// ShapeParams 里每一种可编辑的形状。
#define IDC_SP_VLABEL0                  1020
#define IDC_SP_VX0                      1021
#define IDC_SP_VY0                      1022
#define IDC_SP_VZ0                      1023
#define IDC_SP_VLABEL1                  1024
#define IDC_SP_VX1                      1025
#define IDC_SP_VY1                      1026
#define IDC_SP_VZ1                      1027
#define IDC_SP_VLABEL2                  1028
#define IDC_SP_VX2                      1029
#define IDC_SP_VY2                      1030
#define IDC_SP_VZ2                      1031
#define IDC_SP_SLABEL0                  1032
#define IDC_SP_SEDIT0                   1033
#define IDC_SP_SLABEL1                  1034
#define IDC_SP_SEDIT1                   1035
#define IDC_SP_SLABEL2                  1036
#define IDC_SP_SEDIT2                   1037
#define IDC_SP_SLABEL3                  1038
#define IDC_SP_SEDIT3                   1039
#define IDC_SP_CHK0                     1040
#define IDC_SP_CHK1                     1041
#define IDC_SP_NOTE                     1042

#define IDC_STYLE_COLOR                 1050
#define IDC_STYLE_EDGECOLOR             1051
#define IDC_STYLE_LINEWIDTH             1052
#define IDC_STYLE_POINTSIZE             1053
#define IDC_STYLE_LINESTYLE             1054
#define IDC_STYLE_VISIBLE               1055
#define IDC_STYLE_SELECTABLE            1056
#define IDC_STYLE_SHADOW                1057

#define IDC_TR_TLABEL                   1059
#define IDC_TR_TX                       1060
#define IDC_TR_TY                       1061
#define IDC_TR_TZ                       1062
#define IDC_TR_RX                       1063
#define IDC_TR_RY                       1064
#define IDC_TR_RZ                       1065
#define IDC_TR_SX                       1066
#define IDC_TR_SY                       1067
#define IDC_TR_SZ                       1068

#define IDC_CAM_FOV                     1070
#define IDC_CAM_ORTHO                   1071
#define IDC_CAM_NEAR                    1072
#define IDC_CAM_FAR                     1073

#define IDC_TESS_CHORD                  1080
#define IDC_TESS_ANGULAR                1081

// -- 命令 -------------------------------------------------------------------

#define ID_FILE_SAMPLE                  32771
#define ID_FILE_IMPORT                  32772
#define ID_FILE_EXPORT                  32773
#define ID_FILE_EXPORT_SELECTION        32774
#define ID_FILE_SCREENSHOT              32775

#define ID_EDIT_DESELECT                32780
#define ID_EDIT_RENAME                  32781
#define ID_EDIT_PARAMS                  32782
#define ID_EDIT_COLOR                   32783
#define ID_EDIT_STYLE                   32784
#define ID_EDIT_TRANSFORM               32785
#define ID_EDIT_VISIBLE                 32786
#define ID_EDIT_GROUP                   32787
#define ID_EDIT_UNDO_CAPACITY           32788
#define ID_EDIT_CLEAR_HISTORY           32789

// 连号的一段：菜单、消息映射的 _RANGE 宏和状态同步都按 first + 序号取。
#define ID_LINESTYLE_FIRST              32790
#define ID_LINESTYLE_SOLID              32790
#define ID_LINESTYLE_DASHED             32791
#define ID_LINESTYLE_DOTTED             32792
#define ID_LINESTYLE_DASHDOT            32793
#define ID_LINESTYLE_CENTER             32794
#define ID_LINESTYLE_HIDDEN             32795
#define ID_LINESTYLE_LAST               32795

#define ID_TOOL_FIRST                   32800
#define ID_TOOL_SELECT                  32800
#define ID_TOOL_POINT                   32801
#define ID_TOOL_LINE                    32802
#define ID_TOOL_CIRCLE                  32803
#define ID_TOOL_RECTANGLE               32804
#define ID_TOOL_POLYLINE                32805
#define ID_TOOL_EXTRUDE                 32806
#define ID_TOOL_MOVE                    32807
#define ID_TOOL_ROTATE                  32808
#define ID_TOOL_SCALE                   32809
#define ID_TOOL_MEASURE                 32810
#define ID_TOOL_LAST                    32810

#define ID_CREATE_ARC                   32815
#define ID_WORKPLANE_XY                 32816
#define ID_WORKPLANE_YZ                 32817
#define ID_WORKPLANE_ZX                 32818
#define ID_WORKPLANE_FROM_PICK          32819

#define ID_VIEW_STD_FIRST               32820
#define ID_VIEW_STD_FRONT               32820
#define ID_VIEW_STD_BACK                32821
#define ID_VIEW_STD_RIGHT               32822
#define ID_VIEW_STD_LEFT                32823
#define ID_VIEW_STD_TOP                 32824
#define ID_VIEW_STD_BOTTOM              32825
#define ID_VIEW_STD_ISO                 32826
#define ID_VIEW_STD_LAST                32826

#define ID_VIEW_FIT                     32830
#define ID_VIEW_FIT_SELECTION           32831
#define ID_VIEW_PERSPECTIVE             32832

#define ID_RENDER_FIRST                 32835
#define ID_RENDER_SHADED                32835
#define ID_RENDER_SHADED_EDGES          32836
#define ID_RENDER_WIREFRAME             32837
#define ID_RENDER_HIDDENLINE            32838
#define ID_RENDER_LAST                  32838
#define ID_RENDER_CYCLE                 32839

#define ID_VIEW_GRID                    32840
#define ID_VIEW_HUD                     32841
#define ID_VIEW_BACKGROUND              32842
#define ID_VIEW_CAMERA                  32843

#define ID_PICK_FIRST                   32845
#define ID_PICK_VERTEX                  32845
#define ID_PICK_EDGE                    32846
#define ID_PICK_FACE                    32847
#define ID_PICK_LAST                    32847

#define ID_VIEW_NEW_VIEWPORT            32850
#define ID_VIEW_CLOSE_EXTRA             32851
#define ID_VIEW_TREE_PANE               32852
#define ID_VIEW_LOG_PANE                32853

#define ID_SNAP_FIRST                   32860
#define ID_SNAP_ENDPOINT                32860
#define ID_SNAP_MIDPOINT                32861
#define ID_SNAP_CENTER                  32862
#define ID_SNAP_QUADRANT                32863
#define ID_SNAP_INTERSECTION            32864
#define ID_SNAP_PERPENDICULAR           32865
#define ID_SNAP_GRID                    32866
#define ID_SNAP_LAST                    32866

#define ID_SNAP_TOLERANCE               32870
#define ID_SNAP_CONTINUOUS              32871
#define ID_TESS_PARAMS                  32872

#define ID_MEASURE_LAST_RESULT          32880
#define ID_ENTITY_INFO                  32881
#define ID_UNIT_SETTINGS                32882

#define ID_HELP_SHORTCUTS               32890

#define ID_LOG_FIRST                    32895
#define ID_LOG_TRACE                    32895
#define ID_LOG_DEBUG                    32896
#define ID_LOG_INFO                     32897
#define ID_LOG_WARNING                  32898
#define ID_LOG_ERROR                    32899
#define ID_LOG_FATAL                    32900
#define ID_LOG_OFF                      32901
#define ID_LOG_LAST                     32901

// -- 状态栏窗格（afxres.h 的 ID_INDICATOR_* 用到 0xE705 为止，这里从 59200 起）--

#define ID_INDICATOR_PROMPT             59200
#define ID_INDICATOR_SELECTION          59201
#define ID_INDICATOR_MEASURE            59202
#define ID_INDICATOR_DEVICE             59203

#endif // CADGEOM_MFC_VIEWER_RESOURCE_H
