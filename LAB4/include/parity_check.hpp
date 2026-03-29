#ifndef PARITY_CHECK_HPP
#define PARITY_CHECK_HPP

// 检验一段包含奇偶校验位的二值消息是否通过校验。
//
// 参数：
// - msg：输入消息数组
// - msg_length：数组长度
//
// 约定：
// - msg 的每个元素只区分为两类：
//   - 0 表示二进制 0
//   - 非 0 表示二进制 1
// - 整个消息（包含校验位）采用偶校验：
//   若消息中 1 的总个数为偶数，则校验通过
//
// 返回值：
// - 1：校验通过
// - 0：校验失败
int parity_check(const unsigned char* msg, const int msg_length);

#endif  // PARITY_CHECK_HPP
