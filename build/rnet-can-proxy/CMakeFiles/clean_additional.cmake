# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "external/RNetMsgBroker/CMakeFiles/RNetMsgBrokerTest_autogen.dir/AutogenUsed.txt"
  "external/RNetMsgBroker/CMakeFiles/RNetMsgBrokerTest_autogen.dir/ParseCache.txt"
  "external/RNetMsgBroker/CMakeFiles/RNetMsgBroker_autogen.dir/AutogenUsed.txt"
  "external/RNetMsgBroker/CMakeFiles/RNetMsgBroker_autogen.dir/ParseCache.txt"
  "external/RNetMsgBroker/RNetMsgBrokerTest_autogen"
  "external/RNetMsgBroker/RNetMsgBroker_autogen"
  )
endif()
