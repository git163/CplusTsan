#ifndef CPLUSTSAN_COMPONENTS_CORE_CORE_API_H_
#define CPLUSTSAN_COMPONENTS_CORE_CORE_API_H_

// 返回库版本字符串；TSan 变体编译时（-DTSAN_BUILD）会带上 -tsan 后缀，
// 便于在日志中肉眼确认当前加载的是哪个变体。
const char* core_version();

#endif  // CPLUSTSAN_COMPONENTS_CORE_CORE_API_H_