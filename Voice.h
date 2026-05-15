#ifndef VOICE_H
#define VOICE_H

// GBK编码的语音播报固定文本
// 注意：请将此文件保存为GBK编码，以确保语音模块正确识别

// 固定播报文本
#define V_NOW       "现在"
#define V_TEMP      "温度"
#define V_ROOM      "度，室内温度"
#define V_HUM       "度，湿度百分之"
#define V_TODAY     "今天"
#define V_TOMORROW  "明天"
#define V_AFTER     "后天"
#define V_WEATHER   "天气，白天"
#define V_NIGHT     "，夜间"
#define V_RANGE     "，温度"
#define V_TO        "到"
#define V_RAIN      "度，降水概率百分之"
#define V_WIND      "，"
#define V_LEVEL     "级。"

inline const char* GetWeatherGBK(int code) {
    switch (code) {
        case 0: case 1: case 2: case 3: return "晴";
        case 4:                      return "多云";
        case 5: case 6:              return "晴间多云";
        case 7: case 8:              return "大部多云";
        case 9:                      return "阴";
        case 10:                     return "阵雨";
        case 11:                     return "雷阵雨";
        case 12:                     return "雷阵雨伴有冰雹";
        case 13:                     return "小雨";
        case 14:                     return "中雨";
        case 15:                     return "大雨";
        case 16:                     return "暴雨";
        case 17:                     return "大暴雨";
        case 18:                     return "特大暴雨";
        case 19:                     return "冻雨";
        case 20:                     return "雨夹雪";
        case 21:                     return "阵雪";
        case 22:                     return "小雪";
        case 23:                     return "中雪";
        case 24:                     return "大雪";
        case 25:                     return "暴雪";
        case 26:                     return "浮尘";
        case 27:                     return "扬沙";
        case 28:                     return "沙尘暴";
        case 29:                     return "强沙尘暴";
        case 30:                     return "雾";
        case 31:                     return "霾";
        case 32:                     return "风";
        case 33:                     return "大风";
        case 34:                     return "飓风";
        case 35:                     return "热带风暴";
        case 36:                     return "龙卷风";
        case 37:                     return "冷";
        case 38:                     return "热";
        default:                     return "未知";
    }
}

#endif
