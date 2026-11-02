#pragma once
// An in-process stand-in for the relay, written from how RelayClient.kt uses it. It is NOT the real relay
// (relay/src/core.js was not available): it exists to exercise the client, pairing flow and session logic.
// Each host name is a separate relay with its own rooms. Hosts can be switched off, made slow, or turned
// into a man in the middle that swaps the other member's keys.
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

#include "tc/handshake.hpp"
#include "tc/kdf.hpp"
#include "tc/relay.hpp"
#include "testing.hpp"

struct FakeRelay final : tc::HttpTransport {
    struct Member { std::string id, tokenHash, commit, ik, ek; bool revealed = false; };
    struct Msg { std::int64_t id; std::string env, to; };
    struct Room { std::string codeToken; std::vector<Member> members; std::vector<Msg> msgs; std::int64_t nextId = 1; };
    struct Host {
        std::map<std::string, Room> rooms;
        bool down = false, mitm = false;
        int delayMs = 0;
        int requests = 0;
    };

    std::mutex m;
    std::map<std::string, Host> hosts;
    // the attacker's own consistent key triple, used by mitm hosts
    tc::KeyPair atkIk = tc::KeyPair::fromPrivate(tc::kdf::sha256({tc::asBytes("attacker-ik")}));
    tc::KeyPair atkEk = tc::KeyPair::fromPrivate(tc::kdf::sha256({tc::asBytes("attacker-ek")}));

    void reset() { std::lock_guard<std::mutex> l(m); hosts.clear(); }
    void dropAllRooms(const std::string& h) { std::lock_guard<std::mutex> l(m); hosts[h].rooms.clear(); }
    std::size_t roomCount(const std::string& h) { std::lock_guard<std::mutex> l(m); return hosts[h].rooms.size(); }
    Host& host(const std::string& h) { std::lock_guard<std::mutex> l(m); return hosts[h]; }
    void set(const std::string& h, bool down, bool mitm = false, int delayMs = 0) {
        std::lock_guard<std::mutex> l(m);
        hosts[h].down = down; hosts[h].mitm = mitm; hosts[h].delayMs = delayMs;
    }
    int requests(const std::string& h) { std::lock_guard<std::mutex> l(m); return hosts[h].requests; }
    bool hasRoom(const std::string& h, const std::string& room) { std::lock_guard<std::mutex> l(m); return hosts[h].rooms.count(room) > 0; }
    std::size_t queued(const std::string& h, const std::string& room) {
        std::lock_guard<std::mutex> l(m);
        return hosts[h].rooms[room].msgs.size();
    }

    static tc::HttpResponse reply(int status, const std::string& body) { return {true, status, body}; }
    static tc::HttpResponse err(int status, const char* e) { return reply(status, std::string("{\"error\":\"") + e + "\"}"); }
    static std::string q(const std::string& s) { return tc::jsonQuote(s); }

    tc::HttpResponse send(const tc::HttpRequest& req) override {
        int delay = 0;
        std::string hostName;
        {
            std::lock_guard<std::mutex> l(m);
            auto start = req.url.find("://") + 3;
            auto slash = req.url.find('/', start);
            hostName = req.url.substr(start, slash - start);
            Host& h = hosts[hostName];
            ++h.requests;
            if (h.down) return {false, 0, ""};
            delay = h.delayMs;
        }
        if (delay) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        std::lock_guard<std::mutex> l(m);
        return handle(hosts[hostName], req);
    }

    tc::HttpResponse handle(Host& h, const tc::HttpRequest& req) {
        auto p = req.url.find("/v1/rooms/");
        std::string rest = req.url.substr(p + 10);
        std::string roomId = rest.substr(0, 32), path = rest.substr(32);
        std::int64_t after = 0;
        if (auto qpos = path.find('?'); qpos != std::string::npos) {
            after = std::stoll(path.substr(path.find("after=") + 6));
            path = path.substr(0, qpos);
        }
        auto body = tc::parseJson(req.body.empty() ? "{}" : req.body);
        if (!body) return err(400, "bad json");

        if (path == "/hello") {
            Room& room = h.rooms[roomId];
            if (room.members.empty()) room.codeToken = req.bearer;
            if (req.bearer != room.codeToken) return err(401, "unauthorized");
            std::string id = body->optString("memberId");
            for (auto& mem : room.members)
                if (mem.id == id) return reply(200, "{\"members\":" + std::to_string(room.members.size()) + "}");
            if (room.members.size() >= 2) return err(409, "full");
            room.members.push_back({id, body->optString("memberTokenHash"), body->optString("commit"), "", "", false});
            return reply(200, "{\"members\":" + std::to_string(room.members.size()) + "}");
        }
        auto rit = h.rooms.find(roomId);
        if (rit == h.rooms.end()) return err(404, "not found");
        Room& room = rit->second;
        Member* me = nullptr;
        for (auto& mem : room.members)
            if (auto raw = tc::hexDecode(req.bearer); raw && tc::hexEncode(tc::kdf::sha256({*raw})) == mem.tokenHash) me = &mem;
        if (!me) return err(401, "unauthorized");
        Member* other = nullptr;
        for (auto& mem : room.members) if (&mem != me) other = &mem;
        bool sealed = room.members.size() == 2 && room.members[0].revealed && room.members[1].revealed;

        if (req.method == "DELETE") { h.rooms.erase(rit); return reply(204, ""); }
        if (path == "/state") {
            std::string out = std::string("{\"sealed\":") + (sealed ? "true" : "false") + ",\"members\":[";
            bool firstItem = true;
            for (auto& mem : room.members) {
                std::string commit = mem.commit, ik = mem.ik, ek = mem.ek;
                if (h.mitm && &mem == other) {   // swap the other member's keys for ours, with a matching commitment
                    ik = tc::hexEncode(atkIk.pub); ek = tc::hexEncode(atkEk.pub);
                    commit = tc::hexEncode(tc::handshake::commit(atkIk.pub, atkEk.pub, *tc::hexDecode(mem.id)));
                    if (!mem.revealed) ik = ek = "";
                }
                if (!firstItem) out += ",";
                firstItem = false;
                out += "{\"memberId\":" + q(mem.id) + ",\"commit\":" + q(commit) + ",\"ik\":" + (mem.revealed ? q(ik) : "null") +
                       ",\"ek\":" + (mem.revealed ? q(ek) : "null") + "}";
            }
            return reply(200, out + "]}");
        }
        if (path == "/reveal") {
            if (room.members.size() != 2) return err(400, "not ready");
            auto ik = tc::hexDecode(body->optString("ik")), ek = tc::hexDecode(body->optString("ek")), id = tc::hexDecode(me->id);
            if (!ik || !ek || tc::hexEncode(tc::handshake::commit(*ik, *ek, *id)) != me->commit) return err(400, "commit mismatch");
            me->ik = body->optString("ik"); me->ek = body->optString("ek"); me->revealed = true;
            bool nowSealed = room.members[0].revealed && room.members[1].revealed;
            return reply(200, std::string("{\"sealed\":") + (nowSealed ? "true" : "false") + "}");
        }
        if (path == "/messages" && req.method == "POST") {
            if (!sealed) return err(409, "not sealed");
            std::string env = body->optString("env");
            if (env.size() != 180) return err(400, "bad envelope");
            room.msgs.push_back({room.nextId++, env, other->id});
            return reply(200, "{}");
        }
        if (path == "/messages") {
            if (!sealed) return err(409, "not sealed");
            std::string out = "{\"messages\":[";
            int count = 0; bool more = false;
            for (auto& msg : room.msgs) {
                if (msg.to != me->id || msg.id <= after) continue;
                if (count == 128) { more = true; break; }
                out += (count++ ? "," : "") + std::string("{\"id\":") + std::to_string(msg.id) + ",\"env\":" + q(msg.env) + "}";
            }
            return reply(200, out + "],\"more\":" + (more ? "true" : "false") + "}");
        }
        if (path == "/ack") {
            std::int64_t upTo = body->optInt("upTo", 0);
            std::erase_if(room.msgs, [&](const Msg& x) { return x.to == me->id && x.id <= upTo; });
            return reply(200, "{}");
        }
        return err(404, "no such route");
    }
};
