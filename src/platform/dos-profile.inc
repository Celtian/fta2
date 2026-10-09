// Verified DOS profile; see docs/dos-resources.md and tools/fta2_data.py.
constexpr const char *dosExeHash="a7413ddf57df2ec649e3cb401cfc6b3d0b0141cc840161dbd6b3029e381837c2";
constexpr const char *dosPackHash="f77094a250eae904073bd21b8f2c1c208d10f8ab9d56830893aed7acd62c765e";
constexpr Asset assets[]={
 {"Onyx10Font",0x213C98,3206},
 {"Plate18Font",0x211C48,8270},
 {"Helv11Font",0x214920,3376},
 {"Amber13Font",0x215654,5550},
 {"ThinFix8Font",0x217AEC,2822},
 {"Script10Font",0x216C78,3166},
 {"loadingWindowPalette",0x1C66D0,1024},
 {"loadingWindowData",0x1C6AD0,307200},
 {"ColorMapRanges",0x1C3B24,1584},
 {"closeBx1ImageData",0x1C44CC,144},
 {"closeBx2ImageData",0x1C455C,144},
 {"usePtrImageData",0x1C45EC,225},
 {"xPointerImageData",0x1C46D0,225},
 {"arrowImageData",0x1C47B4,187},
 {"grabPtrImageData",0x1C4870,208},
 {"attakPtrImageData",0x1C4940,529},
 {"centerActorIndicatorImageData",0x1C4B54,81},
 {"pgUpImageData",0x1C4BB0,256},
 {"pgDownImageData",0x1C4CB0,256},
 {"pgLeftImageData",0x1C4DB0,256},
 {"pgRightImageData",0x1C4EB0,256},
 {"autoWalkImageData",0x1C4FB0,225},
 {"gaugeImageData",0x1C39E8,240},
};
constexpr struct { const char *name,*hash; } archives[]={
 {"FTADATA.HRS","3b6d0475b1187e28d4387750391ce1d2a7791ab0fd7e706c1040ff16576cb0b5"},
 {"FTAIMAGE.HRS","e8f0cf92507a11c1c6ec0bf790fa054a883d421689f5f3abd9b4e2c8f9172bec"},
 {"FTA.HRS","1d4fc56861bdc3f0e1dda1a21f48991e350dd8213fb694484f0f7fa731c7055d"},
 {"FTASOUND.HRS","7eb5cc6a582abcfe91ab6b50d343c95cdc68be1fa439e464592fed9f7cbcfbf8"},
 {"SCRIPTS.HRS","ddb3747ed47e1d6a420165d28e1d7f3e66abd64e1323690c11620b910dd05317"},
};
constexpr struct { const char *name,*hash; } banks[]={
 {"DRIVERS/SAMPLE.AD","5044c392ad31b42a8218614c53172dd48ac01855aa5c683ae9d31466f33b50b1"},
 {"DRIVERS/SAMPLE.OPL","5044c392ad31b42a8218614c53172dd48ac01855aa5c683ae9d31466f33b50b1"},
};
