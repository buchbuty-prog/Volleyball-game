#include "vc26/Commentary.h"

#include <algorithm>

namespace vc26 {

namespace {

const char* const kSurnames[] = {
    "Aranda", "Bakker", "Castell", "Dimitrov", "Ekwueme", "Farrow", "Galvan", "Haddad", "Ivanic", "Jansen",
    "Kaya", "Lindqvist", "Marin", "Nowak", "Okoro", "Pereira", "Quist", "Rahimi", "Sato", "Tamura",
    "Ulloa", "Varga", "Weber", "Xavier", "Yilmaz", "Zapata", "Abara", "Benali", "Conti", "Duarte",
    "Eriksen", "Fontaine", "Grabowski", "Hosseini", "Ikeda", "Juarez", "Kovac", "Lemaire", "Mbeki", "Nieminen"};
constexpr std::uint32_t kSurnameCount = sizeof(kSurnames) / sizeof(kSurnames[0]);

template <std::size_t N>
std::string pickOne(const char* const (&lines)[N], std::uint32_t pick) {
    return lines[pick % N];
}

std::string fill(std::string line, const PointCall& call) {
    auto replace = [&line](const std::string& key, const std::string& value) {
        for (std::size_t at = line.find(key); at != std::string::npos; at = line.find(key, at + value.size())) {
            line.replace(at, key.size(), value);
        }
    };
    replace("{scorer}", call.scorer);
    replace("{blunder}", call.blunder);
    replace("{team}", call.team);
    replace("{contacts}", std::to_string(call.contacts));
    replace("{run}", std::to_string(call.run));
    return line;
}

}  // namespace

// FNV-1a, so a team's names don't change between runs or machines.
std::uint32_t nameHash(const std::string& text) {
    std::uint32_t hash = 2166136261u;
    for (const char c : text) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
    }
    return hash;
}

Roster makeRoster(const std::string& teamName) {
    // Walking the list in steps coprime with its length never repeats a name.
    const std::uint32_t hash = nameHash(teamName);
    const std::uint32_t steps[] = {3, 7, 9, 11, 13, 17};
    const std::uint32_t step = steps[(hash >> 8) % 6];
    Roster roster;
    for (std::uint32_t i = 0; i < kRosterSize; ++i) {
        roster[i] = kSurnames[(hash + i * step) % kSurnameCount];
    }
    return roster;
}

int PointRun::add(Team winner) {
    length_ = length_ > 0 && winner == last_ ? length_ + 1 : 1;
    last_ = winner;
    return length_;
}

std::string callPoint(const PointCall& call, std::uint32_t pick) {
    static const char* const kKill[] = {"{scorer} hammers it down!", "Nobody touches that. {scorer} with the kill.", "{scorer} finds the floor.", "Too good from {scorer}."};
    static const char* const kAce[] = {"Ace! {scorer} leaves them standing.", "{scorer} from the service line. Unplayable.", "Straight to the floor. Ace for {scorer}."};
    static const char* const kBlock[] = {"Stuffed by {scorer}!", "{scorer} shuts the door at the net.", "Roof! {scorer} says no."};
    static const char* const kDown[] = {"It drops. Nobody calls it.", "The floor wins that one.", "Point {team}. That one fell between them."};
    static const char* const kOut[] = {"Out! {blunder} gives that one away.", "That's out. A gift for {team}.", "{blunder} can't keep it in."};
    static const char* const kAntenna[] = {"Off the antenna! {blunder} can't believe it.", "That clips the antenna. Point {team}."};
    static const char* const kNet[] = {"Net touch, and the point goes to {team}.", "{blunder} clips the net."};

    std::string line;
    switch (call.reason) {
        case PointReason::Kill: line = pickOne(kKill, pick); break;
        case PointReason::Ace: line = pickOne(kAce, pick); break;
        case PointReason::Block: line = pickOne(kBlock, pick); break;
        case PointReason::BallDown: line = pickOne(kDown, pick); break;
        case PointReason::Out: line = pickOne(kOut, pick); break;
        case PointReason::Antenna: line = pickOne(kAntenna, pick); break;
        case PointReason::NetTouch: line = pickOne(kNet, pick); break;
    }
    // Without a name the line would read wrong, so it falls back to the team.
    const bool needsScorer = line.find("{scorer}") != std::string::npos && call.scorer.empty();
    const bool needsBlunder = line.find("{blunder}") != std::string::npos && call.blunder.empty();
    if (needsScorer || needsBlunder) line = "Point {team}.";

    std::string extra;
    if (call.contacts >= 12) {
        extra = " What a rally, {contacts} touches!";
    } else if (call.setOnTheLine) {
        extra = " And that was with the set on the line.";
    } else if (call.run >= 4) {
        extra = " {run} in a row for {team}.";
    }
    return fill(line + extra, call);
}

}  // namespace vc26
