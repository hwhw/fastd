FIND_PATH(NACL_INCLUDE_DIR crypto_secretbox_xsalsa20poly1305.h PATH_SUFFIXES nacl)
FIND_LIBRARY(NACL_LIBRARY NAMES nacl)

IF (NACL_INCLUDE_DIR AND NACL_LIBRARY)
   SET(NACL_FOUND TRUE)
ENDIF (NACL_INCLUDE_DIR AND NACL_LIBRARY)

IF (NACL_FOUND)
   IF (NOT NaCl_FIND_QUIETLY)
      MESSAGE(STATUS "Found NaCl: Networking and Cryptography library: ${NACL_LIBRARY}; include path: ${NACL_INCLUDE_DIR}")
   ENDIF (NOT NaCl_FIND_QUIETLY)
ELSE (NACL_FOUND)
   IF (NaCl_FIND_REQUIRED)
      MESSAGE(FATAL_ERROR "Could not find NaCl: Networking and Cryptography library")
   ENDIF (NaCl_FIND_REQUIRED)
ENDIF (NACL_FOUND)
