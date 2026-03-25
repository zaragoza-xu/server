#pragma once

#include <memory>
#include <string>

class Channel;

class User {
private:
  int avatarType;
  std::string userName;
  std::shared_ptr<Channel> channel;
  int roomId;
  std::string uid;

public:
  User(int uid, const std::string &userName,
       std::shared_ptr<Channel> channel, int avatarType = 0)
      : avatarType(avatarType), userName(userName), channel(channel),
        roomId(-1), uid(std::to_string(uid)) {}

  const std::string &get_username() const { return userName; }
  std::shared_ptr<Channel> get_channel() const { return channel; }
  int get_avatar_type() const { return avatarType; }
  const std::string &get_uid() const { return uid; }

  int get_room_id() const { return roomId; }
  void set_room_id(int roomId) { this->roomId = roomId; }

  bool is_in_room() const { return roomId != -1; }
};
