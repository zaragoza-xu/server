#pragma once
#include <format>
#include <iostream>

#include "types.h"

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
  case Protocol::HomeRequestType::HEARTBEAT:
    return "HEARTBEAT";
  case Protocol::HomeRequestType::EDIT_PROFILE:
    return "EDIT_PROFILE";
  case Protocol::HomeRequestType::SET_READY:
    return "SET_READY";
  case Protocol::HomeRequestType::BROADCAST:
    return "BROADCAST";
  case Protocol::HomeRequestType::GET_STATE_STATUS:
    return "GET_STATE_STATUS";
  case Protocol::HomeRequestType::ERROR:
    return "ERROR";
  }
  return "UNKNOWN_HOME_TYPE";
}

inline std::string_view request_type_name(Protocol::ShopResponseType type) {
  switch (type) {
  case Protocol::ShopResponseType::SHOP_INIT:
    return "SHOP_INIT";
  case Protocol::ShopResponseType::SHOP_MOVE_CURSOR:
    return "SHOP_MOVE_CURSOR";
  case Protocol::ShopRequestType::SHOP_BUY:
    return "SHOP_BUY";
  case Protocol::ShopRequestType::ERROR:
    return "ERROR";
  }
  return "UNKNOWN_SHOP_TYPE";
}

inline std::string_view request_type_name(Protocol::ShopResponseType type) {
  switch (type) {
  case Protocol::ShopResponseType::SHOP_SYNC:
    return "SHOP_SYNC";
  }
  return "UNKNOWN_SHOP_RESPONSE_TYPE";
}

inline std::string_view request_type_name(Protocol::MapRequestType type) {
  switch (type) {
  case Protocol::MapRequestType::MAP_INIT:
    return "MAP_INIT";
  case Protocol::MapRequestType::MAP_MOVE:
    return "MAP_MOVE";
  }
  return "UNKNOWN_MAP_TYPE";
}
} // namespace logging
