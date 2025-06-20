
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <map>
#include <unordered_set>

namespace fs = std::filesystem;

// ======= Helper Functions =======

std::string generate_simple_hash(const std::string& content) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::hash<std::string> hasher;
    std::string data_to_hash = content + std::to_string(timestamp) + std::to_string(hasher(content));
    size_t content_hash = hasher(data_to_hash);
    std::stringstream ss;
    ss << std::hex << std::setw(32) << std::setfill('0') << content_hash;
    return ss.str();
}

// ======= CommitNode Class =======

class CommitNode {
public:
    std::string hash, message, timestamp, parent_hash;
    std::vector<std::string> parent_hashes;
    std::map<std::string, std::string> tracked_files;

    CommitNode(const std::string& msg, const std::string& parent_h, const std::map<std::string, std::string>& files_map)
        : message(msg), parent_hash(parent_h), tracked_files(files_map) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
        timestamp = ss.str();
    }

    std::string serialize() const {
        std::stringstream ss;
        ss << "tree\n";
        for (const auto& pair : tracked_files)
            ss << "blob " << pair.second << " " << pair.first << "\n";
        ss << "parent " << parent_hash << "\n";
        ss << "author Dev <dev@example.com> " << timestamp << "\n";
        ss << "committer Dev <dev@example.com> " << timestamp << "\n\n";
        ss << message << "\n";
        return ss.str();
    }

    static CommitNode deserialize(const std::string& hash, const std::string& content) {
        std::string msg = "", parent = "", ts = "";
        std::map<std::string, std::string> files;
        std::istringstream iss(content);
        std::string line;
        bool msg_block = false;
        while (getline(iss, line)) {
            if (line.empty()) { msg_block = true; continue; }
            if (msg_block) msg += line + "\n";
            else if (line.rfind("blob ", 0) == 0) {
                std::istringstream blob_line_ss(line);
                std::string type, hash_val, filename;
                blob_line_ss >> type >> hash_val >> filename;
                files[filename] = hash_val;
            } else if (line.rfind("parent ", 0) == 0)
                parent = line.substr(7);
            else if (line.rfind("author ", 0) == 0)
                ts = line.substr(line.length() - 19, 19);
        }
        if (!msg.empty() && msg.back() == '\n') msg.pop_back();
        CommitNode node(msg, parent, files);
        node.hash = hash;
        node.timestamp = ts.empty() ? node.timestamp : ts;
        return node;
    }
};

// ======= MiniGit Class =======

class MiniGit {
public:
    MiniGit() : minigit_dir_name_(".minigit") {}

    void init();
    void add(const std::string& filename);
    void commit(const std::string& message);
    void log();
    void branch(const std::string& name);
    void merge(const std::string& branch_name);

private:
    std::string minigit_dir_name_;
    std::map<std::string, std::string> staging_area_;

    std::string save_blob(const std::string& content);
    std::string read_blob(const std::string& hash);
    void save_commit_object(const CommitNode& commit);
    CommitNode read_commit_object(const std::string& hash);
    std::string get_head_commit_hash();
    void update_head(const std::string& hash);
    std::string read_file(const std::string& path);
    std::unordered_set<std::string> getAncestors(const std::string& hash);
    CommitNode find_common_ancestor(const std::string& hash1, const std::string& hash2);
};

// Definitions of MiniGit methods go here
// (same as included in the full merged code provided previously)
// ...

// ======= Main Function =======

int main(int argc, char* argv[]) {
    MiniGit minigit;
    if (argc < 2) {
        std::cout << "Usage: minigit <command> [args]\n";
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "init") minigit.init();
    else if (cmd == "add" && argc >= 3) minigit.add(argv[2]);
    else if (cmd == "commit" && argc >= 4 && std::string(argv[2]) == "-m") minigit.commit(argv[3]);
    else if (cmd == "log") minigit.log();
    else if (cmd == "branch" && argc >= 3) minigit.branch(argv[2]);
    else if (cmd == "merge" && argc >= 3) minigit.merge(argv[2]);
    else std::cout << "Unknown or invalid command.\n";

    return 0;
}
