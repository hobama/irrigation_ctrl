#ifndef VERSION_H
#define VERSION_H

#ifndef PACKAGE_NAME
#define PACKAGE_NAME "Irrigation Controller"
#endif

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "0.7"
#endif

#ifndef GIT_VERSION
#define GIT_VERSION "unknown"
#endif

#define VERSION_STRING (PACKAGE_NAME " " PACKAGE_VERSION " (git:" GIT_VERSION ")")

#define OTA_VERSION_STRING (PACKAGE_VERSION "-" GIT_VERSION)

#endif
