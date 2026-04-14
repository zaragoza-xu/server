#pragma once
#include <format>
#include <iostream>

#include "protocol.h"

namespace logging {
template <typename... Args> void log(std::string_view fmt, Args &&...args) {

  std::string msg = std::vformat(fmt, std::make_format_args(args...));
  std::cerr << msg << std::endl;
}

inline std::string_view request_type_name(Protocol::LoginRequestType type) {
  switch (type) {
  case Protocol::LoginRequestType::LOGIN:
    return "LOGIN";
  case Protocol::LoginRequestType::REGISTER:
    return "REGISTER";
  case Protocol::LoginRequestType::LOGOUT:
    return "LOGOUT";
  case Protocol::LoginRequestType::ERROR:
    return "ERROR";
  }
  return "UNKNOWN_LOGIN_TYPE";
}

inline std::string_view request_type_name(Protocol::HomeRequestType type) {
  switch (type) {
  case Protocol::HomeRequestType::CREATE_ROOM:
    return "CREATE_ROOM";
  case Protocol::HomeRequestType::JOIN_ROOM:
    return "JOIN_ROOM";
  case Protocol::HomeRequestType::LEAVE_ROOM:
    return "LEAVE_ROOM";
  case Protocol::HomeRequestType::LIST_ROOMS:
    return "LIST_ROOMS";
  case Protocol::HomeRequestType::SEND_MESSAGE:
    return "SEND_MESSAGE";
  case Protocol::HomeRequestType::EDIT_PROFILE:
    return "EDIT_PROFILE";
  case Protocol::HomeRequestType::SET_READY:
    return "SET_READY";
  case Protocol::HomeRequestType::ERROR:
    return "ERROR";
  }
  return "UNKNOWN_HOME_TYPE";
}
} // namespace logging
