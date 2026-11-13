# GUI & 2D/3D Drawing (DX & CEGUI)

2D screen drawing, fonts, text, rectangles, lines, textures, render targets, GUI windows, buttons, edits, labels, gridlists, and 3D line drawing.

## Parity Status

- **Total Functions:** 199
- **Implemented:** 22 (11.1%)
- **Stubs:** 6
- **Missing:** 171

> **Technical Requirement:** Desktop uses CEGUI and Direct3D9. Mobile uses `MtaDxView.java` (Android Canvas) + `LuaGuiApi.cpp`. Missing shaders (HLSL -> GLSL ES) and complex CEGUI widgets (gridlist, tabpanel, scrollpane).

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `addModel2DFX` | Missing | `CLua2DFXDefs.cpp:568` | `AddModel2DFX` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `dxConvertPixels` | Missing | `CLuaDrawingDefs.cpp:60` | `DxConvertPixels` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxCreateFont` | Missing | `CLuaDrawingDefs.cpp:43` | `DxCreateFont` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxCreateRenderTarget` | Missing | `CLuaDrawingDefs.cpp:46` | `DxCreateRenderTarget` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxCreateScreenSource` | Missing | `CLuaDrawingDefs.cpp:47` | `DxCreateScreenSource` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxCreateShader` | Missing | `CLuaDrawingDefs.cpp:45` | `DxCreateShader` | Needs GLSL ES 3.0 shader pipeline (translating desktop D3DX9 HLSL). |
| `dxCreateTexture` | Missing | `CLuaDrawingDefs.cpp:44` | `DxCreateTexture` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxDrawCircle` | *Stub* | `CLuaDrawingDefs.cpp:31` | `DxDrawCircle` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `dxDrawImage` | *Stub* | `CLuaDrawingDefs.cpp:32` | `DxDrawImage` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `dxDrawImageSection` | Missing | `CLuaDrawingDefs.cpp:33` | `DxDrawImageSection` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxDrawLine` | Missing | `CLuaDrawingDefs.cpp:25` | `DxDrawLine` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxDrawLine3D` | Missing | `CLuaDrawingDefs.cpp:28` | `DxDrawLine3D` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxDrawMaterialLine3D` | Missing | `CLuaDrawingDefs.cpp:26` | `DxDrawMaterialLine3D` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxDrawMaterialPrimitive` | Missing | `CLuaDrawingDefs.cpp:36` | `DxDrawMaterialPrimitive` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxDrawMaterialPrimitive3D` | Missing | `CLuaDrawingDefs.cpp:37` | `DxDrawMaterialPrimitive3D` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxDrawMaterialSectionLine3D` | Missing | `CLuaDrawingDefs.cpp:27` | `DxDrawMaterialSectionLine3D` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxDrawModel3D` | Missing | `CLuaDrawingDefs.cpp:39` | `ArgumentParser<DxDrawModel3D>` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxDrawPrimitive` | *Stub* | `CLuaDrawingDefs.cpp:34` | `DxDrawPrimitive` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `dxDrawPrimitive3D` | Missing | `CLuaDrawingDefs.cpp:35` | `DxDrawPrimitive3D` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxDrawRectangle` | **Implemented** | `CLuaDrawingDefs.cpp:30` | `DxDrawRectangle` | Operational on Android. (l_dxDrawRectangle) |
| `dxDrawText` | **Implemented** | `CLuaDrawingDefs.cpp:29` | `DxDrawText` | Operational on Android. (l_dxDrawText) |
| `dxDrawWiredSphere` | Missing | `CLuaDrawingDefs.cpp:38` | `ArgumentParser<DxDrawWiredSphere>` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxGetBlendMode` | Missing | `CLuaDrawingDefs.cpp:64` | `DxGetBlendMode` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxGetFontHeight` | **Implemented** | `CLuaDrawingDefs.cpp:42` | `DxGetFontHeight` | Operational on Android. (DxGetFontHeight) |
| `dxGetMaterialSize` | Missing | `CLuaDrawingDefs.cpp:48` | `DxGetMaterialSize` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxGetPixelColor` | Missing | `CLuaDrawingDefs.cpp:61` | `DxGetPixelColor` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxGetPixelsFormat` | Missing | `CLuaDrawingDefs.cpp:59` | `DxGetPixelsFormat` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxGetPixelsSize` | Missing | `CLuaDrawingDefs.cpp:58` | `DxGetPixelsSize` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxGetStatus` | Missing | `CLuaDrawingDefs.cpp:54` | `DxGetStatus` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxGetTextSize` | Missing | `CLuaDrawingDefs.cpp:41` | `ArgumentParser<DxGetTextSize>` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxGetTextWidth` | **Implemented** | `CLuaDrawingDefs.cpp:40` | `DxGetTextWidth` | Operational on Android. (DxGetTextWidth) |
| `dxGetTexturePixels` | Missing | `CLuaDrawingDefs.cpp:56` | `DxGetTexturePixels` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxIsAspectRatioAdjustmentEnabled` | Missing | `CLuaDrawingDefs.cpp:66` | `DxIsAspectRatioAdjustmentEnabled` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxSetAspectRatioAdjustmentEnabled` | Missing | `CLuaDrawingDefs.cpp:65` | `DxSetAspectRatioAdjustmentEnabled` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxSetBlendMode` | Missing | `CLuaDrawingDefs.cpp:63` | `DxSetBlendMode` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxSetPixelColor` | Missing | `CLuaDrawingDefs.cpp:62` | `DxSetPixelColor` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxSetRenderTarget` | Missing | `CLuaDrawingDefs.cpp:52` | `DxSetRenderTarget` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxSetShaderTessellation` | Missing | `CLuaDrawingDefs.cpp:50` | `DxSetShaderTessellation` | Needs GLSL ES 3.0 shader pipeline (translating desktop D3DX9 HLSL). |
| `dxSetShaderTransform` | Missing | `CLuaDrawingDefs.cpp:51` | `DxSetShaderTransform` | Needs GLSL ES 3.0 shader pipeline (translating desktop D3DX9 HLSL). |
| `dxSetShaderValue` | Missing | `CLuaDrawingDefs.cpp:49` | `DxSetShaderValue` | Needs GLSL ES 3.0 shader pipeline (translating desktop D3DX9 HLSL). |
| `dxSetTestMode` | Missing | `CLuaDrawingDefs.cpp:55` | `DxSetTestMode` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxSetTextureEdge` | Missing | `CLuaDrawingDefs.cpp:67` | `DxSetTextureEdge` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxSetTexturePixels` | Missing | `CLuaDrawingDefs.cpp:57` | `DxSetTexturePixels` | Needs Android Canvas / OpenGL ES draw line routing. |
| `dxUpdateScreenSource` | Missing | `CLuaDrawingDefs.cpp:53` | `DxUpdateScreenSource` | Needs Android Canvas / OpenGL ES draw line routing. |
| `getChatboxCharacterLimit` | Missing | `CLuaGUIDefs.cpp:54` | `ArgumentParser<GUIGetChatboxCharacterLimit>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getChatboxLayout` | Missing | `CLuaGUIDefs.cpp:186` | `GUIGetChatboxLayout` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getModel2DFXCount` | Missing | `CLua2DFXDefs.cpp:573` | `GetModel2DFXCount` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getModel2DFXEffects` | Missing | `CLua2DFXDefs.cpp:572` | `GetModel2DFXEffects` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getModel2DFXPosition` | Missing | `CLua2DFXDefs.cpp:571` | `GetModel2DFXPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getModel2DFXProperty` | Missing | `CLua2DFXDefs.cpp:572` | `GetModel2DFXProperty` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getModel2DFXType` | Missing | `CLua2DFXDefs.cpp:573` | `GetModel2DFXType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `guiBlur` | Missing | `CLuaGUIDefs.cpp:132` | `GUIBlur` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiBringToFront` | Missing | `CLuaGUIDefs.cpp:130` | `GUIBringToFront` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCheckBoxGetSelected` | Missing | `CLuaGUIDefs.cpp:149` | `GUICheckBoxGetSelected` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCheckBoxSetSelected` | Missing | `CLuaGUIDefs.cpp:135` | `GUICheckBoxSetSelected` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiComboBoxAddItem` | Missing | `CLuaGUIDefs.cpp:188` | `GUIComboBoxAddItem` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiComboBoxClear` | Missing | `CLuaGUIDefs.cpp:190` | `GUIComboBoxClear` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiComboBoxGetItemCount` | Missing | `CLuaGUIDefs.cpp:195` | `GUIComboBoxGetItemCount` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiComboBoxGetItemText` | Missing | `CLuaGUIDefs.cpp:193` | `GUIComboBoxGetItemText` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiComboBoxGetSelected` | Missing | `CLuaGUIDefs.cpp:191` | `GUIComboBoxGetSelected` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiComboBoxIsOpen` | Missing | `CLuaGUIDefs.cpp:197` | `GUIComboBoxIsOpen` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiComboBoxRemoveItem` | Missing | `CLuaGUIDefs.cpp:189` | `GUIComboBoxRemoveItem` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiComboBoxSetItemText` | Missing | `CLuaGUIDefs.cpp:194` | `GUIComboBoxSetItemText` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiComboBoxSetOpen` | Missing | `CLuaGUIDefs.cpp:196` | `GUIComboBoxSetOpen` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiComboBoxSetSelected` | Missing | `CLuaGUIDefs.cpp:192` | `GUIComboBoxSetSelected` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateButton` | **Implemented** | `CLuaGUIDefs.cpp:58` | `GUICreateButton` | Operational on Android. (GuiCreateButton) |
| `guiCreateCheckBox` | Missing | `CLuaGUIDefs.cpp:67` | `GUICreateCheckBox` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateComboBox` | Missing | `CLuaGUIDefs.cpp:70` | `GUICreateComboBox` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateEdit` | Missing | `CLuaGUIDefs.cpp:59` | `GUICreateEdit` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateFont` | Missing | `CLuaGUIDefs.cpp:71` | `GUICreateFont` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateGridList` | Missing | `CLuaGUIDefs.cpp:61` | `GUICreateGridList` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiCreateLabel` | **Implemented** | `CLuaGUIDefs.cpp:57` | `GUICreateLabel` | Operational on Android. (GuiCreateLabel) |
| `guiCreateMemo` | Missing | `CLuaGUIDefs.cpp:60` | `GUICreateMemo` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateProgressBar` | Missing | `CLuaGUIDefs.cpp:66` | `GUICreateProgressBar` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateRadioButton` | Missing | `CLuaGUIDefs.cpp:68` | `GUICreateRadioButton` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateScrollBar` | Missing | `CLuaGUIDefs.cpp:63` | `GUICreateScrollBar` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateScrollPane` | Missing | `CLuaGUIDefs.cpp:62` | `GUICreateScrollPane` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateStaticImage` | Missing | `CLuaGUIDefs.cpp:69` | `GUICreateStaticImage` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateTab` | Missing | `CLuaGUIDefs.cpp:65` | `GUICreateTab` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateTabPanel` | Missing | `CLuaGUIDefs.cpp:64` | `GUICreateTabPanel` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiCreateWindow` | **Implemented** | `CLuaGUIDefs.cpp:56` | `GUICreateWindow` | Operational on Android. (GuiCreateWindow) |
| `guiDeleteTab` | Missing | `CLuaGUIDefs.cpp:77` | `GUIDeleteTab` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiEditGetCaretIndex` | Missing | `CLuaGUIDefs.cpp:158` | `GUIEditGetCaretIndex` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiEditGetMaxLength` | Missing | `CLuaGUIDefs.cpp:162` | `GUIEditGetMaxLength` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiEditIsMasked` | Missing | `CLuaGUIDefs.cpp:160` | `GUIEditIsMasked` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiEditIsReadOnly` | Missing | `CLuaGUIDefs.cpp:164` | `GUIEditIsReadOnly` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiEditSetCaretIndex` | Missing | `CLuaGUIDefs.cpp:157` | `GUIEditSetCaretIndex` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiEditSetMasked` | Missing | `CLuaGUIDefs.cpp:159` | `GUIEditSetMasked` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiEditSetMaxLength` | Missing | `CLuaGUIDefs.cpp:161` | `GUIEditSetMaxLength` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiEditSetReadOnly` | Missing | `CLuaGUIDefs.cpp:163` | `GUIEditSetReadOnly` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiFocus` | Missing | `CLuaGUIDefs.cpp:133` | `GUIFocus` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiGetAlpha` | **Implemented** | `CLuaGUIDefs.cpp:141` | `GUIGetAlpha` | Operational on Android. (GuiGetAlpha) |
| `guiGetCursorType` | Missing | `CLuaGUIDefs.cpp:147` | `GUIGetCursorType` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiGetEnabled` | **Implemented** | `CLuaGUIDefs.cpp:138` | `GUIGetEnabled` | Operational on Android. (GuiGetEnabled) |
| `guiGetFont` | Missing | `CLuaGUIDefs.cpp:143` | `GUIGetFont` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiGetInputEnabled` | **Implemented** | `CLuaGUIDefs.cpp:40` | `GUIGetInputEnabled` | Operational on Android. (GuiGetInputEnabled) |
| `guiGetInputMode` | Missing | `CLuaGUIDefs.cpp:42` | `GUIGetInputMode` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiGetPosition` | **Implemented** | `CLuaGUIDefs.cpp:145` | `GUIGetPosition` | Operational on Android. (GuiGetPosition) |
| `guiGetProperties` | Missing | `CLuaGUIDefs.cpp:140` | `GUIGetProperties` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiGetProperty` | Missing | `CLuaGUIDefs.cpp:139` | `GUIGetProperty` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiGetScreenSize` | **Implemented** | `CLuaGUIDefs.cpp:155` | `GUIGetScreenSize` | Operational on Android. (l_guiGetScreenSize) |
| `guiGetSelectedTab` | Missing | `CLuaGUIDefs.cpp:75` | `GUIGetSelectedTab` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiGetSize` | **Implemented** | `CLuaGUIDefs.cpp:144` | `GUIGetSize` | Operational on Android. (GuiGetSize) |
| `guiGetText` | **Implemented** | `CLuaGUIDefs.cpp:142` | `GUIGetText` | Operational on Android. (GuiGetText) |
| `guiGetVisible` | **Implemented** | `CLuaGUIDefs.cpp:146` | `GUIGetVisible` | Operational on Android. (GuiGetVisible) |
| `guiGridListAddColumn` | Missing | `CLuaGUIDefs.cpp:81` | `GUIGridListAddColumn` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListAddRow` | Missing | `CLuaGUIDefs.cpp:90` | `GUIGridListAddRow` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListAutoSizeColumn` | Missing | `CLuaGUIDefs.cpp:93` | `GUIGridListAutoSizeColumn` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListClear` | Missing | `CLuaGUIDefs.cpp:94` | `GUIGridListClear` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetColumnCount` | Missing | `CLuaGUIDefs.cpp:89` | `GUIGridListGetColumnCount` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetColumnTitle` | Missing | `CLuaGUIDefs.cpp:86` | `GUIGridListGetColumnTitle` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetColumnWidth` | Missing | `CLuaGUIDefs.cpp:84` | `GUIGridListGetColumnWidth` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetHorizontalScrollPosition` | Missing | `CLuaGUIDefs.cpp:108` | `GUIGridListGetHorizontalScrollPosition` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetItemColor` | Missing | `CLuaGUIDefs.cpp:100` | `GUIGridListGetItemColor` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetItemData` | Missing | `CLuaGUIDefs.cpp:98` | `GUIGridListGetItemData` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetItemText` | Missing | `CLuaGUIDefs.cpp:96` | `GUIGridListGetItemText` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetRowCount` | Missing | `CLuaGUIDefs.cpp:88` | `GUIGridListGetRowCount` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetSelectedCount` | Missing | `CLuaGUIDefs.cpp:105` | `GUIGridListGetSelectedCount` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetSelectedItem` | Missing | `CLuaGUIDefs.cpp:103` | `GUIGridListGetSelectedItem` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetSelectedItems` | Missing | `CLuaGUIDefs.cpp:104` | `GUIGridListGetSelectedItems` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetSelectionMode` | Missing | `CLuaGUIDefs.cpp:102` | `GUIGridListGetSelectionMode` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListGetVerticalScrollPosition` | Missing | `CLuaGUIDefs.cpp:110` | `GUIGridListGetVerticalScrollPosition` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListInsertRowAfter` | Missing | `CLuaGUIDefs.cpp:91` | `GUIGridListInsertRowAfter` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListIsSortingEnabled` | Missing | `CLuaGUIDefs.cpp:80` | `GUIGridListIsSortingEnabled` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListRemoveColumn` | Missing | `CLuaGUIDefs.cpp:82` | `GUIGridListRemoveColumn` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListRemoveRow` | Missing | `CLuaGUIDefs.cpp:92` | `GUIGridListRemoveRow` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetColumnTitle` | Missing | `CLuaGUIDefs.cpp:85` | `GUIGridListSetColumnTitle` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetColumnWidth` | Missing | `CLuaGUIDefs.cpp:83` | `GUIGridListSetColumnWidth` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetHorizontalScrollPosition` | Missing | `CLuaGUIDefs.cpp:107` | `GUIGridListSetHorizontalScrollPosition` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetItemColor` | Missing | `CLuaGUIDefs.cpp:99` | `GUIGridListSetItemColor` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetItemData` | Missing | `CLuaGUIDefs.cpp:97` | `GUIGridListSetItemData` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetItemText` | Missing | `CLuaGUIDefs.cpp:95` | `GUIGridListSetItemText` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetScrollBars` | Missing | `CLuaGUIDefs.cpp:87` | `GUIGridListSetScrollBars` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetSelectedItem` | Missing | `CLuaGUIDefs.cpp:106` | `GUIGridListSetSelectedItem` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetSelectionMode` | Missing | `CLuaGUIDefs.cpp:101` | `GUIGridListSetSelectionMode` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetSortingEnabled` | Missing | `CLuaGUIDefs.cpp:79` | `GUIGridListSetSortingEnabled` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiGridListSetVerticalScrollPosition` | Missing | `CLuaGUIDefs.cpp:109` | `GUIGridListSetVerticalScrollPosition` | Needs native Android ListView / RecyclerView or custom CEGUI widget port. |
| `guiLabelGetColor` | Missing | `CLuaGUIDefs.cpp:174` | `GUILabelGetColor` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiLabelGetFontHeight` | Missing | `CLuaGUIDefs.cpp:179` | `GUILabelGetFontHeight` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiLabelGetTextExtent` | Missing | `CLuaGUIDefs.cpp:178` | `GUILabelGetTextExtent` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiLabelSetColor` | Missing | `CLuaGUIDefs.cpp:173` | `GUILabelSetColor` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiLabelSetHorizontalAlign` | Missing | `CLuaGUIDefs.cpp:176` | `GUILabelSetHorizontalAlign` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiLabelSetVerticalAlign` | Missing | `CLuaGUIDefs.cpp:175` | `GUILabelSetVerticalAlign` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiMemoGetCaretIndex` | Missing | `CLuaGUIDefs.cpp:167` | `GUIMemoGetCaretIndex` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiMemoGetVerticalScrollPosition` | Missing | `CLuaGUIDefs.cpp:171` | `GUIMemoGetVerticalScrollPosition` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiMemoIsReadOnly` | Missing | `CLuaGUIDefs.cpp:169` | `GUIMemoIsReadOnly` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiMemoSetCaretIndex` | Missing | `CLuaGUIDefs.cpp:166` | `GUIMemoSetCaretIndex` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiMemoSetReadOnly` | Missing | `CLuaGUIDefs.cpp:168` | `GUIMemoSetReadOnly` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiMemoSetVerticalScrollPosition` | Missing | `CLuaGUIDefs.cpp:170` | `GUIMemoSetVerticalScrollPosition` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiMoveToBack` | Missing | `CLuaGUIDefs.cpp:131` | `GUIMoveToBack` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiProgressBarGetProgress` | Missing | `CLuaGUIDefs.cpp:153` | `GUIProgressBarGetProgress` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiProgressBarSetProgress` | Missing | `CLuaGUIDefs.cpp:152` | `GUIProgressBarSetProgress` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiRadioButtonGetSelected` | Missing | `CLuaGUIDefs.cpp:150` | `GUIRadioButtonGetSelected` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiRadioButtonSetSelected` | Missing | `CLuaGUIDefs.cpp:136` | `GUIRadioButtonSetSelected` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiScrollBarGetScrollPosition` | Missing | `CLuaGUIDefs.cpp:119` | `GUIScrollBarGetScrollPosition` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiScrollBarSetScrollPosition` | Missing | `CLuaGUIDefs.cpp:118` | `GUIScrollBarSetScrollPosition` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiScrollPaneGetHorizontalScrollPosition` | Missing | `CLuaGUIDefs.cpp:114` | `GUIScrollPaneGetHorizontalScrollPosition` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiScrollPaneGetVerticalScrollPosition` | Missing | `CLuaGUIDefs.cpp:116` | `GUIScrollPaneGetVerticalScrollPosition` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiScrollPaneSetHorizontalScrollPosition` | Missing | `CLuaGUIDefs.cpp:113` | `GUIScrollPaneSetHorizontalScrollPosition` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiScrollPaneSetScrollBars` | Missing | `CLuaGUIDefs.cpp:112` | `GUIScrollPaneSetScrollBars` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiScrollPaneSetVerticalScrollPosition` | Missing | `CLuaGUIDefs.cpp:115` | `GUIScrollPaneSetVerticalScrollPosition` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiSetAlpha` | **Implemented** | `CLuaGUIDefs.cpp:123` | `GUISetAlpha` | Operational on Android. (GuiSetAlpha) |
| `guiSetEnabled` | **Implemented** | `CLuaGUIDefs.cpp:121` | `GUISetEnabled` | Operational on Android. (GuiSetEnabled) |
| `guiSetFont` | Missing | `CLuaGUIDefs.cpp:125` | `GUISetFont` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiSetInputEnabled` | **Implemented** | `CLuaGUIDefs.cpp:41` | `GUISetInputEnabled` | Operational on Android. (GuiSetInputEnabled) |
| `guiSetInputMode` | Missing | `CLuaGUIDefs.cpp:43` | `GUISetInputMode` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiSetPosition` | **Implemented** | `CLuaGUIDefs.cpp:127` | `GUISetPosition` | Operational on Android. (GuiSetPosition) |
| `guiSetProperty` | Missing | `CLuaGUIDefs.cpp:122` | `GUISetProperty` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiSetSelectedTab` | Missing | `CLuaGUIDefs.cpp:76` | `GUISetSelectedTab` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiSetSize` | **Implemented** | `CLuaGUIDefs.cpp:126` | `GUISetSize` | Operational on Android. (GuiSetSize) |
| `guiSetText` | **Implemented** | `CLuaGUIDefs.cpp:124` | `GUISetText` | Operational on Android. (GuiSetText) |
| `guiSetVisible` | **Implemented** | `CLuaGUIDefs.cpp:128` | `GUISetVisible` | Operational on Android. (GuiSetVisible) |
| `guiStaticImageGetNativeSize` | Missing | `CLuaGUIDefs.cpp:74` | `GUIStaticImageGetNativeSize` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiStaticImageLoadImage` | Missing | `CLuaGUIDefs.cpp:73` | `GUIStaticImageLoadImage` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiWindowIsMovable` | Missing | `CLuaGUIDefs.cpp:183` | `GUIWindowIsMovable` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiWindowIsSizable` | Missing | `CLuaGUIDefs.cpp:184` | `GUIWindowIsSizable` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiWindowSetMovable` | Missing | `CLuaGUIDefs.cpp:181` | `GUIWindowSetMovable` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiWindowSetSizable` | Missing | `CLuaGUIDefs.cpp:182` | `GUIWindowSetSizable` | Needs Android widget state and input handler in LuaGuiApi. |
| `isChatBoxInputActive` | *Stub* | `CLuaGUIDefs.cpp:45` | `GUIIsChatBoxInputActive` | Currently stubbed: `l_false: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `isConsoleActive` | *Stub* | `CLuaGUIDefs.cpp:46` | `GUIIsConsoleActive` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `isDebugViewActive` | Missing | `CLuaGUIDefs.cpp:47` | `GUIIsDebugViewActive` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isMTAWindowActive` | Missing | `CLuaGUIDefs.cpp:50` | `GUIIsMTAWindowActive` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isMainMenuActive` | *Stub* | `CLuaGUIDefs.cpp:49` | `GUIIsMainMenuActive` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `isTransferBoxActive` | Missing | `CLuaGUIDefs.cpp:51` | `GUIIsTransferBoxActive` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeModel2DFX` | Missing | `CLua2DFXDefs.cpp:568` | `RemoveModel2DFX` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetModel2DFXEffects` | Missing | `CLua2DFXDefs.cpp:569` | `ResetModel2DFXEffects` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetModel2DFXPosition` | Missing | `CLua2DFXDefs.cpp:571` | `ResetModel2DFXPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetModel2DFXProperty` | Missing | `CLua2DFXDefs.cpp:570` | `ResetModel2DFXProperty` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `restoreModel2DFX` | Missing | `CLua2DFXDefs.cpp:568` | `RestoreModel2DFX` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setChatboxCharacterLimit` | Missing | `CLuaGUIDefs.cpp:53` | `ArgumentParser<GUISetChatboxCharacterLimit>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDebugViewActive` | Missing | `CLuaGUIDefs.cpp:48` | `GUISetDebugViewActive` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setModel2DFXPosition` | Missing | `CLua2DFXDefs.cpp:569` | `SetModel2DFXPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setModel2DFXProperty` | Missing | `CLua2DFXDefs.cpp:570` | `SetModel2DFXProperty` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `svgCreate` | Missing | `CLuaVectorGraphicDefs.cpp:17` | `ArgumentParser<SVGCreate>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `svgGetDocumentXML` | Missing | `CLuaVectorGraphicDefs.cpp:18` | `ArgumentParser<SVGGetDocumentXML>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `svgGetSize` | Missing | `CLuaVectorGraphicDefs.cpp:20` | `ArgumentParser<SVGGetSize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `svgSetDocumentXML` | Missing | `CLuaVectorGraphicDefs.cpp:19` | `ArgumentParser<SVGSetDocumentXML>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `svgSetSize` | Missing | `CLuaVectorGraphicDefs.cpp:21` | `ArgumentParser<SVGSetSize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `svgSetUpdateCallback` | Missing | `CLuaVectorGraphicDefs.cpp:22` | `ArgumentParser<SVGSetUpdateCallback>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
