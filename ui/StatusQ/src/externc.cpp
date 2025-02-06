#include <QtGlobal>

#include <StatusQ/typesregistration.h>

extern "C" {

Q_DECL_EXPORT void statusq_registerQmlTypes() {
    registerStatusQTypes();
}

#ifdef Q_OS_IOS
Q_DECL_EXPORT int main(int argc, char *argv[]) {
    return 0;
}
#endif

} // extern "C"
