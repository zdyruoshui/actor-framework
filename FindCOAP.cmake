# - Try to find coap
# Once done this will define
#  
#  COAP_FOUND        - system has coap
#  COAP_INCLUDE_DIR  - the coap include directory
#  COAP_LIBRARIES    - link these to use coap
#

# Unix style platforms

#message(***** COAP_INCLUDE_DIR: "${COAP_INCLUDE_DIR}" ********)
message("-- Looking for CoAP")

find_path(COAP_INCLUDE_DIR 
    NAMES coap.h 
    PATHS ENV ../libcoap/
)
find_library(COAP_LIBRARIES
    NAMES libcoap.a 
    PATHS ENV LD_LIBRARY_PATH ../libcoap/
)
#message("coap_libraries: ${COAP_LIBRARIES}")

#message(***** COAP ENV: "$ENV{GPU_SDK}" ********)

SET( COAP_FOUND "NO" )
IF(COAP_LIBRARIES )
    SET( COAP_FOUND "YES" )
    message("-- Looking for CoAP -- found")
ELSE()
    message("-- Looking for CoAP -- not found")
ENDIF(COAP_LIBRARIES)

MARK_AS_ADVANCED(
  COAP_INCLUDE_DIR
)

