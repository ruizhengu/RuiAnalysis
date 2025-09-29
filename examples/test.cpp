#include <iostream>

class MyClass {
public:
    void method1(int x) {
    }

    int method2() { return 42; }
};

void regularFunction() {
    // std::cout << d << std::endl;
    MyClass m;
    // m.method1(d);
    m.method2();
}

int main() {
    MyClass obj;
    obj.method1(10);
    regularFunction();
}
