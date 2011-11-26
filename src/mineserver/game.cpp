/*
  Copyright (c) 2011, The Mineserver Project
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the The Mineserver Project nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <iostream>
#include <vector>
#include <algorithm>

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

#include <mineserver/localization.h>
#include <mineserver/network/client.h>
#include <mineserver/network/message/chat.h>
#include <mineserver/network/message/login.h>
#include <mineserver/network/message/handshake.h>
#include <mineserver/network/message/chunkprepare.h>
#include <mineserver/network/message/chunk.h>
#include <mineserver/network/message/spawnposition.h>
#include <mineserver/network/message/windowitems.h>
#include <mineserver/network/message/position.h>
#include <mineserver/network/message/orientation.h>
#include <mineserver/network/message/positionandorientation.h>
#include <mineserver/network/message/namedentityspawn.h>
#include <mineserver/network/message/destroyentity.h>
#include <mineserver/network/message/entityteleport.h>
#include <mineserver/network/message/entityrelativemove.h>
#include <mineserver/network/message/entityrelativemoveandlook.h>
#include <mineserver/network/message/digging.h>
#include <mineserver/network/message/blockplacement.h>
#include <mineserver/network/message/blockchange.h>
#include <mineserver/network/message/serverlistping.h>
#include <mineserver/network/message/kick.h>
#include <mineserver/game.h>

bool is_dead(Mineserver::Network_Client::pointer_t client) {
  return client->alive() == false;
}

void Mineserver::Game::run()
{
  for (clientList_t::iterator client_it=m_clients.begin();client_it!=m_clients.end();++client_it) {
    Mineserver::Network_Client::pointer_t client(*client_it);

    if (!client) {
      std::cout << "Got an invalid pointer somehow..." << std::endl;
      continue;
    }

    // 1200 in-game ticks = timed out, inactive ticks = ticks past since last keep-alive:
    if (client->inactiveTicks() > timeOutTicks) {
      std::cout << "Client timed-out." << std::endl;
      client->timedOut();
    }

    if (!client->alive()) {
      std::cout << "Found a dead client." << std::endl;

      if (m_clientMap.find(client) != m_clientMap.end()) {
        m_players.erase(m_clientMap[client]->getName());
        m_clientMap.erase(client);
      }

      continue;
    }

    std::cout << "There are " << client->incoming().size() << " messages." << std::endl;

    for (std::vector<Mineserver::Network_Message::pointer_t>::iterator message_it=client->incoming().begin();message_it!=client->incoming().end();++message_it) {
      Mineserver::Network_Message::pointer_t message = *message_it;
      m_messageWatchers[message->mid](shared_from_this(), client, message);
    }

    std::cout << "Watchers done." << std::endl;

    client->incoming().clear();

    client->run();

    client->write();
  }

  m_clients.erase(remove_if(m_clients.begin(), m_clients.end(), is_dead), m_clients.end());

  for (playerList_t::iterator player_it=m_players.begin();player_it!=m_players.end();++player_it) {
    Mineserver::Game_Player::pointer_t player(player_it->second);
    player->run();
  }
}

void Mineserver::Game::messageWatcherKeepAlive(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "KeepAlive watcher called!" << std::endl;

  client->resetInactiveTicks();
}

void Mineserver::Game::messageWatcherHandshake(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "Handshake watcher called!" << std::endl;

  boost::shared_ptr<Mineserver::Network_Message_Handshake> response = boost::make_shared<Mineserver::Network_Message_Handshake>();
  response->mid = 0x02;
  response->username = "-";
  client->outgoing().push_back(response);
}

void Mineserver::Game::messageWatcherChat(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "Chat watcher called!" << std::endl;

  const Mineserver::Network_Message_Chat* msg = reinterpret_cast<Mineserver::Network_Message_Chat*>(&(*message));

  chatPostWatcher(shared_from_this(), getPlayerForClient(client), msg->message);
}

void Mineserver::Game::messageWatcherLogin(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "Login watcher called!" << std::endl;

  const Mineserver::Network_Message_Login* msg = reinterpret_cast<Mineserver::Network_Message_Login*>(&(*message));

  Mineserver::World::pointer_t world = getWorld(0);

  std::cout << "Player login v." << msg->version << ": " << msg->username << std::endl;

  Mineserver::Game_Player::pointer_t player;
  if (m_players.find(msg->username) == m_players.end()) {
    player = boost::make_shared<Mineserver::Game_Player>();
    player->setName(msg->username);
    player->setEid(getNextEid());
    player->getPosition().x = world->getSpawnPosition().x;
    player->getPosition().y = world->getSpawnPosition().y;
    player->getPosition().z = world->getSpawnPosition().z;
    player->getPosition().stance = player->getPosition().y + 1.62;
    player->getPosition().onGround = true;
    addPlayer(player);
  } else {
    player = m_players[msg->username];
  }

  associateClient(client, player);

  boost::shared_ptr<Mineserver::Network_Message_Login> loginMessage = boost::make_shared<Mineserver::Network_Message_Login>();
  loginMessage->mid = 0x01;
  loginMessage->version = 22;
  loginMessage->seed = world->getWorldSeed();
  loginMessage->mode = world->getGameMode();
  loginMessage->dimension = world->getDimension();
  loginMessage->difficulty = world->getDifficulty();
  loginMessage->worldHeight = world->getWorldHeight();
  loginMessage->maxPlayers = 32; // this determines how many slots the tab window will have
  client->outgoing().push_back(loginMessage);

  for (int x = -5; x <= 5; ++x) {
    for (int z = -5; z <= 5; ++z) {
      boost::shared_ptr<Mineserver::Network_Message_ChunkPrepare> chunkPrepareMessage = boost::make_shared<Mineserver::Network_Message_ChunkPrepare>();
      chunkPrepareMessage->mid = 0x32;
      chunkPrepareMessage->x = x;
      chunkPrepareMessage->z = z;
      chunkPrepareMessage->mode = 1;
      client->outgoing().push_back(chunkPrepareMessage);
    }
  }

  for (int x = -5; x <= 5; ++x) {
    for (int z = -5; z <= 5; ++z) {
      boost::shared_ptr<Mineserver::Network_Message_Chunk> chunkMessage = boost::make_shared<Mineserver::Network_Message_Chunk>();
      chunkMessage->mid = 0x33;
      chunkMessage->posX = x * 16;
      chunkMessage->posY = 0;
      chunkMessage->posZ = z * 16;
      chunkMessage->sizeX = 15;
      chunkMessage->sizeY = 127;
      chunkMessage->sizeZ = 15;
      chunkMessage->chunk = world->generateChunk(x, z);
      client->outgoing().push_back(chunkMessage);
    }
  }

  boost::shared_ptr<Mineserver::Network_Message_SpawnPosition> spawnPositionMessage = boost::make_shared<Mineserver::Network_Message_SpawnPosition>();
  spawnPositionMessage->mid = 0x06;
  spawnPositionMessage->x = world->getSpawnPosition().x;
  spawnPositionMessage->y = world->getSpawnPosition().y;
  spawnPositionMessage->z = world->getSpawnPosition().z;
  client->outgoing().push_back(spawnPositionMessage);

  boost::shared_ptr<Mineserver::Network_Message_WindowItems> windowItemsMessage = boost::make_shared<Mineserver::Network_Message_WindowItems>();
  windowItemsMessage->mid = 0x68;
  windowItemsMessage->windowId = -1;
  windowItemsMessage->count = 0;
  client->outgoing().push_back(windowItemsMessage);

  std::cout << "Spawning player at " << player->getPosition().x << "," << player->getPosition().y << "," << player->getPosition().z << std::endl;

  boost::shared_ptr<Mineserver::Network_Message_PositionAndOrientation> positionAndOrientationMessage = boost::make_shared<Mineserver::Network_Message_PositionAndOrientation>();
  positionAndOrientationMessage->mid = 0x0D;
  positionAndOrientationMessage->x = player->getPosition().x;
  positionAndOrientationMessage->y = player->getPosition().y;
  positionAndOrientationMessage->z = player->getPosition().z;
  positionAndOrientationMessage->stance = player->getPosition().stance;
  positionAndOrientationMessage->yaw = player->getPosition().yaw;
  positionAndOrientationMessage->pitch = player->getPosition().pitch;
  positionAndOrientationMessage->onGround = player->getPosition().onGround;
  client->outgoing().push_back(positionAndOrientationMessage);
}

void Mineserver::Game::messageWatcherPosition(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "Position watcher called!" << std::endl;

  const Mineserver::Network_Message_Position* msg = reinterpret_cast<Mineserver::Network_Message_Position*>(&(*message));

  if (clientIsAssociated(client)) {
    Mineserver::Game_Player::pointer_t player = getPlayerForClient(client);
    Mineserver::Game_PlayerPosition position(msg->x, msg->y, msg->z, msg->stance, player->getPosition().yaw, player->getPosition().pitch, msg->onGround);
    movementPostWatcher(shared_from_this(), getPlayerForClient(client), position);
  }
}

void Mineserver::Game::messageWatcherOrientation(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "Orientation watcher called!" << std::endl;

  const Mineserver::Network_Message_Orientation* msg = reinterpret_cast<Mineserver::Network_Message_Orientation*>(&(*message));

  if (clientIsAssociated(client)) {
    Mineserver::Game_Player::pointer_t player = getPlayerForClient(client);
    Mineserver::Game_PlayerPosition position(player->getPosition().x, player->getPosition().y, player->getPosition().z, player->getPosition().stance, msg->yaw, msg->pitch, msg->onGround);
    movementPostWatcher(shared_from_this(), getPlayerForClient(client), position);
  }
}

void Mineserver::Game::messageWatcherPositionAndOrientation(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "PositionAndOrientation watcher called!" << std::endl;

  const Mineserver::Network_Message_PositionAndOrientation* msg = reinterpret_cast<Mineserver::Network_Message_PositionAndOrientation*>(&(*message));

  if (clientIsAssociated(client)) {
    Mineserver::Game_Player::pointer_t player = getPlayerForClient(client);
    Mineserver::Game_PlayerPosition position(msg->x, msg->y, msg->z, msg->stance, msg->yaw, msg->pitch, msg->onGround);
    movementPostWatcher(shared_from_this(), getPlayerForClient(client), position);
  }
}

void Mineserver::Game::messageWatcherDigging(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "Digging watcher called!" << std::endl;

  const Mineserver::Network_Message_Digging* msg = reinterpret_cast<Mineserver::Network_Message_Digging*>(&(*message));
  if (!clientIsAssociated(client)) { return; }

  // status 0x00: start digging
  // status 0x02: finish digging
  // status 0x04: drop item
  // status 0x05: shoot arrow

  if (msg->status != 2) { return; }

  Mineserver::World::pointer_t world = getWorld(0);

  int chunk_x, chunk_z;
  chunk_x = ((msg->x) >> 4);
  chunk_z = ((msg->z) >> 4);

  if (!world->hasChunk(chunk_x, chunk_z))
  {
    std::cout << "Chunk " << chunk_x << "," << chunk_z << " not found!" << std::endl;
  }
  else
  {
    Mineserver::World_Chunk::pointer_t chunk = world->getChunk(chunk_x, chunk_z);
    Mineserver::World_ChunkPosition cPosition = Mineserver::World_ChunkPosition(msg->x & 15, msg->y, msg->z & 15);
    Mineserver::WorldBlockPosition wPosition = Mineserver::WorldBlockPosition(msg->x, msg->y, msg->z);

    chunk->setBlockType(cPosition.x, cPosition.y, cPosition.z, 0);
    chunk->setBlockMeta(cPosition.x, cPosition.y, cPosition.z, 0);

    blockBreakPostWatcher(shared_from_this(), getPlayerForClient(client), world, wPosition, chunk, cPosition);
  }
}

void Mineserver::Game::messageWatcherBlockPlacement(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "BlockPlacement watcher called!" << std::endl;

  const Mineserver::Network_Message_BlockPlacement* msg = reinterpret_cast<Mineserver::Network_Message_BlockPlacement*>(&(*message));
  if (!clientIsAssociated(client)) { return; }
  //if (msg->itemId==-1) { return; }

  Mineserver::World::pointer_t world = game->getWorld(0);

  int chunk_x, chunk_z;
  chunk_x = ((msg->x) >> 4);
  chunk_z = ((msg->z) >> 4);

  if (!world->hasChunk(chunk_x, chunk_z))
  {
    std::cout << "Chunk " << chunk_x << "," << chunk_z << " not found!" << std::endl;
  }
  else
  {
    Mineserver::World_Chunk::pointer_t chunk = world->getChunk(chunk_x, chunk_z);
    Mineserver::World_ChunkPosition cPosition = Mineserver::World_ChunkPosition(msg->x & 15, msg->y, msg->z & 15);
    Mineserver::WorldBlockPosition wPosition = Mineserver::WorldBlockPosition(msg->x, msg->y, msg->z);

    uint8_t type = chunk->getBlockType(cPosition.x, cPosition.y, cPosition.z);
    uint8_t meta = chunk->getBlockMeta(cPosition.x, cPosition.y, cPosition.z);

    // TODO: Do pre/post block interaction
  }
}

void Mineserver::Game::messageWatcherBlockChange(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "BlockChange watcher called!" << std::endl;
  const Mineserver::Network_Message_BlockChange* msg = reinterpret_cast<Mineserver::Network_Message_BlockChange*>(&(*message));

  Mineserver::World::pointer_t world = game->getWorld(0);

  int chunk_x, chunk_z;
  chunk_x = ((msg->x) >> 4);
  chunk_z = ((msg->z) >> 4);

  if (!world->hasChunk(chunk_x, chunk_z))
  {
    std::cout << "Chunk " << chunk_x << "," << chunk_z << " not found!" << std::endl;
  }
  else
  {
    Mineserver::World_Chunk::pointer_t chunk = world->getChunk(chunk_x, chunk_z);
    Mineserver::World_ChunkPosition cPosition = Mineserver::World_ChunkPosition(msg->x & 15, msg->y, msg->z & 15);
    Mineserver::WorldBlockPosition wPosition = Mineserver::WorldBlockPosition(msg->x, msg->y, msg->z);

    chunk->setBlockType(cPosition.x, cPosition.y, cPosition.z, msg->type);
    chunk->setBlockMeta(cPosition.x, cPosition.y, cPosition.z, msg->meta);

    blockPlacePostWatcher(shared_from_this(), getPlayerForClient(client), world, wPosition, chunk, cPosition, msg->type, msg->meta);
  }
}

void Mineserver::Game::messageWatcherServerListPing(Mineserver::Game::pointer_t game, Mineserver::Network_Client::pointer_t client, Mineserver::Network_Message::pointer_t message)
{
  std::cout << "ServerListPing watcher called!" << std::endl;

  std::stringstream reason;
  reason << "Mineserver 2.0§" << game->countPlayers() << "§" << 32; // TODO: Get max players

  boost::shared_ptr<Mineserver::Network_Message_Kick> response = boost::make_shared<Mineserver::Network_Message_Kick>();
  response->mid = 0xFF;
  response->reason = reason.str(); 
  client->outgoing().push_back(response);
}

bool Mineserver::Game::chatPostWatcher(Mineserver::Game::pointer_t game, Mineserver::Game_Player::pointer_t player, std::string message)
{
  boost::shared_ptr<Mineserver::Network_Message_Chat> chatMessage = boost::make_shared<Mineserver::Network_Message_Chat>();
  chatMessage->mid = 0x03;
  chatMessage->message = "<" + player->getName() + "> " + message;

  for (clientList_t::iterator it = m_clients.begin(); it != m_clients.end(); ++it) {
    (*it)->outgoing().push_back(chatMessage);
  }

  return true;
}

bool Mineserver::Game::movementPostWatcher(Mineserver::Game::pointer_t game, Mineserver::Game_Player::pointer_t player, Mineserver::Game_PlayerPosition position)
{
  std::cout << "movementPostWatcher called!" << std::endl;

  Mineserver::Game_PlayerPosition oldPos = player->getPosition();
  player->setPosition(position);
  boost::shared_ptr<Mineserver::Network_Message> player_move;
  double dX = (oldPos.x - position.x)*(-1);
  double dY = (oldPos.y - position.y)*(-1);
  double dZ = (oldPos.z - position.z)*(-1);
  if( dX > 4 || dX < -4 || dY > 4 || dY < -4 || dZ > 4 || dZ < -4 ) {
    // send player teleport 0x22
    boost::shared_ptr<Mineserver::Network_Message_EntityTeleport> tmp = boost::make_shared<Mineserver::Network_Message_EntityTeleport>();
    tmp->mid = 0x22;
    tmp->entityId = player->getEid();
    tmp->x = (int32_t)position.x*32;
    tmp->y = (int32_t)position.y*32;
    tmp->z = (int32_t)position.z*32;
    tmp->pitch = (int8_t)position.yaw;
    tmp->yaw = (int8_t)position.pitch;
    player_move = tmp;
    std::cout << "player teleported by " << dX << ":" << dY << ":" << dZ << std::endl;
  } else {
    // TODO: check if we moved, if not => use 0x20
    if(oldPos.yaw != position.yaw || oldPos.pitch != position.pitch) {
      // send 0x21
      boost::shared_ptr<Mineserver::Network_Message_EntityRelativeMoveAndLook> tmp = boost::make_shared<Mineserver::Network_Message_EntityRelativeMoveAndLook>();
      tmp->mid = 0x21;
      tmp->entityId = player->getEid();
      tmp->x = (char)(dX * 32);
      tmp->y = (char)(dY * 32);
      tmp->z = (char)(dZ * 32);
      tmp->yaw = (int8_t)position.pitch;
      tmp->pitch = (int8_t)position.yaw;
      player_move = tmp;
    } else {
      // send 0xF1
      boost::shared_ptr<Mineserver::Network_Message_EntityRelativeMove> tmp = boost::make_shared<Mineserver::Network_Message_EntityRelativeMove>();
      tmp->mid = 0xF1;
      tmp->entityId = player->getEid();
      tmp->x = (char)(dX * 32);
      tmp->y = (char)(dY * 32);
      tmp->z = (char)(dZ * 32);
      player_move = tmp;
    }
    std::cout << "player moved by " << dX << ":" << dY << ":" << dZ << std::endl;
  }
  std::cout << "new player position: " << position.x << ":" << position.y << ":" << position.z << std::endl; 
  uint8_t in_distance = 16*3;    // 160 => 10 chunks
  uint8_t out_distance = 16*5;   // 192 => 12 chunks
  // check if we in range of another player now
  double delta_x, delta_y, old_distance, new_distance;

  entityIdSet_t others = m_playerInRange[player];
  clientList_t other_clients;
  clientList_t my_clients = getClientsForPlayer(player);

  std::cout << "others: ";
  for(entityIdSet_t::iterator it=others.begin();it!=others.end();it++) { std::cout << (*it) << ", "; }
  std::cout << std::endl;
  
  for (playerList_t::iterator player_it=m_players.begin();player_it!=m_players.end();++player_it) {
    Mineserver::Game_Player::pointer_t other(player_it->second);
    if(other == player) { 
        continue;
    }
    other_clients = getClientsForPlayer(other);

    // calc new distance
    delta_x = position.x - other->getPosition().x;
    delta_y = position.y - other->getPosition().y;
    new_distance = sqrt(delta_x*delta_x+delta_y*delta_y);
    std::cout << " [" << other->getEid() << "] in range of [" << player->getEid() << "]?" << std::endl;
    std::cout << " [" << other->getEid() << "] distance to [" << player->getEid() << "]: " << new_distance << std::endl;
    if(others.count(other->getEid()) >= 1) {  // we are in range of this one
      if(new_distance > out_distance) { // but now we are out
        // send destroy entity 
        boost::shared_ptr<Mineserver::Network_Message_DestroyEntity> destroyEntity = boost::make_shared<Mineserver::Network_Message_DestroyEntity>();
        destroyEntity->mid = 0x1D;
        destroyEntity->entityId = player->getEid();
        for(clientList_t::iterator it=other_clients.begin();it != other_clients.end(); it++) {
            std::cout << " [" << other->getEid() << "] << destroy entity #" + player->getEid() << std::endl;
          (*it)->outgoing().push_back(destroyEntity);
        }
        // destroy entity on both sides!
        destroyEntity = boost::make_shared<Mineserver::Network_Message_DestroyEntity>();
        destroyEntity->mid = 0x1D;
        destroyEntity->entityId = other->getEid();
        for(clientList_t::iterator it=my_clients.begin();it!=my_clients.end();it++) {
            std::cout << " [" << player->getEid() << "] << destroy entity #" + other->getEid() << std::endl;
          (*it)->outgoing().push_back(destroyEntity);
        }
        // remove player from set
        others.erase(other->getEid());
        std::cout << "     he was" << std::endl;
      } else { // still range
        // update entity position => send 
        for(clientList_t::iterator it=other_clients.begin();it != other_clients.end(); it++) {
            (*it)->outgoing().push_back(player_move);
        }
        std::cout << "     still is" << std::endl;
      }
    } else { // player is NOT in range of this one
      if(new_distance < in_distance) { // but we just entered the range
        boost::shared_ptr<Mineserver::Network_Message_NamedEntitySpawn> spawnEntity = boost::make_shared<Mineserver::Network_Message_NamedEntitySpawn>();
        spawnEntity->mid = 0x14;
        spawnEntity->entityId = player->getEid();
        spawnEntity->name     = player->getName();
        spawnEntity->x        = (int32_t)position.x*32;
        spawnEntity->y        = (int32_t)position.y*32;
        spawnEntity->z        = (int32_t)position.z*32;
        spawnEntity->rotation = (int8_t)position.pitch;
        spawnEntity->pitch    = (int8_t)position.yaw;
        spawnEntity->currentItem = 0;
        for(clientList_t::iterator it=other_clients.begin();it != other_clients.end(); it++) {
          std::cout << " [" << other->getEid() << "] << spawn entity #" << player->getEid() << std::endl;
          (*it)->outgoing().push_back(spawnEntity);
        }
        spawnEntity = boost::make_shared<Mineserver::Network_Message_NamedEntitySpawn>();
        spawnEntity->mid = 0x14;
        spawnEntity->entityId = other->getEid();
        spawnEntity->name     = other->getName();
        spawnEntity->x        = (int32_t)other->getPosition().x*32;
        spawnEntity->y        = (int32_t)other->getPosition().y*32;
        spawnEntity->z        = (int32_t)other->getPosition().z*32;
        spawnEntity->rotation = (int8_t)other->getPosition().pitch;
        spawnEntity->pitch    = (int8_t)other->getPosition().yaw;
        spawnEntity->currentItem = 0;
        for(clientList_t::iterator it=my_clients.begin();it != my_clients.end(); it++) {
          std::cout << " [" << player->getEid() << "] << spawn entity #" << other->getEid() << std::endl;
          (*it)->outgoing().push_back(spawnEntity);
        }
        others.insert(other->getEid());
        m_playerInRange[other].insert(player->getEid());
        std::cout << "      is now" << std::endl;
      }
    }
  }
  std::cout << "others: ";
  for(entityIdSet_t::iterator it=others.begin();it!=others.end();it++) { std::cout << (*it) << ", "; }
  std::cout << std::endl;
  m_playerInRange[player] = others;
  return true;
}

bool Mineserver::Game::blockBreakPostWatcher(Mineserver::Game::pointer_t game, Mineserver::Game_Player::pointer_t player, Mineserver::World::pointer_t world, Mineserver::WorldBlockPosition wPosition, Mineserver::World_Chunk::pointer_t chunk, Mineserver::World_ChunkPosition cPosition)
{
  std::cout << "blockBreakPostWatcher called!" << std::endl;

  boost::shared_ptr<Mineserver::Network_Message_BlockChange> blockChangeMessage = boost::make_shared<Mineserver::Network_Message_BlockChange>();
  blockChangeMessage->mid = 0x35;
  blockChangeMessage->x = wPosition.x;
  blockChangeMessage->y = wPosition.y;
  blockChangeMessage->z = wPosition.z;
  blockChangeMessage->type = 0;
  blockChangeMessage->meta = 0;

  for (clientList_t::iterator it = m_clients.begin(); it != m_clients.end(); ++it) {
    (*it)->outgoing().push_back(blockChangeMessage);
  }

  return true;
}

bool Mineserver::Game::blockPlacePostWatcher(Mineserver::Game::pointer_t game, Mineserver::Game_Player::pointer_t player, Mineserver::World::pointer_t world, Mineserver::WorldBlockPosition wPosition, Mineserver::World_Chunk::pointer_t chunk, Mineserver::World_ChunkPosition cPosition, uint8_t type, uint8_t meta)
{
  std::cout << "blockPlacePostWatcher called!" << std::endl;

  boost::shared_ptr<Mineserver::Network_Message_BlockChange> blockChangeMessage = boost::make_shared<Mineserver::Network_Message_BlockChange>();
  blockChangeMessage->mid = 0x35;
  blockChangeMessage->x = wPosition.x;
  blockChangeMessage->y = wPosition.y;
  blockChangeMessage->z = wPosition.z;
  blockChangeMessage->type = type;
  blockChangeMessage->meta = meta;

  for (clientList_t::iterator it = m_clients.begin(); it != m_clients.end(); ++it) {
    (*it)->outgoing().push_back(blockChangeMessage);
  }

  return true;
}
