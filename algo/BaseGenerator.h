#ifndef BASEGENERATOR_H
#define BASEGENERATOR_H

// ==========================================
//  Cell 生成器抽象基类
//  所有具体生成器（QuotaShuffle、RandomQuota 等）继承此基类
// ==========================================
class BaseGenerator {
public:
    virtual ~BaseGenerator() = default;
    virtual int next() = 0;
};

#endif // BASEGENERATOR_H
