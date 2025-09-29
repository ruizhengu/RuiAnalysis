#include <iostream>

class MyClass {
public:
    void method1(int x) {}
    int method2() { return 42; }
};

void regularFunction(double d) {
    std::cout << d << std::endl;
}

int main() {
    MyClass obj;
    obj.method1(10);
    return obj.method2();
}
