// MiniGit C++11 version with basic add, commit, and log functionality

// Updated MiniGit with working commit functionality using staging area
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <map>
#include <ctime>
#include <chrono>
#include <iomanip>

#ifdef _WIN32
#include <direct.h>
#include <errno.h>
#else
#include <sys/stat.h>
#include <errno.h>
#endif

bool file_exists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

bool create_directory(const std::string& path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

std::string generate_simple_hash(const std::string& content) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::hash<std::string> hasher;
    std::string data_to_hash = content + std::to_string(timestamp) + std::to_string(hasher(content));
    size_t content_hash = hasher(data_to_hash);
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << content_hash;
    return ss.str();
}

class CommitNode {
public:
    std::string hash;
    std::string message;
    std::string timestamp;
    std::vector<std::string> parent_hashes;
    std::map<std::string, std::string> tracked_files;
    static CommitNode deserialize(const std::string& hash_from_file, const std::string& content);

    CommitNode() {}

    CommitNode(const std::string& msg, const std::vector<std::string>& parents, const std::map<std::string, std::string>& files_map)
        : message(msg), parent_hashes(parents), tracked_files(files_map) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
        timestamp = ss.str();
    }

        std::string serialize() const {
        std::stringstream ss;
        ss << "tree\n";
        for (std::map<std::string, std::string>::const_iterator it = tracked_files.begin(); it != tracked_files.end(); ++it) {
            ss << "blob " << it->second << " " << it->first << "\n";
        }
        for (size_t i = 0; i < parent_hashes.size(); ++i) {
            ss << "parent " << parent_hashes[i] << "\n";
        }
        ss << "author MiniGit <minigit@example.com> " << timestamp << "\n";
        ss << "committer MiniGit <minigit@example.com> " << timestamp << "\n\n";
        ss << message << "\n";
        return ss.str();
    }
};

CommitNode CommitNode::deserialize(const std::string& hash_from_file, const std::string& content) {
    std::string msg = "";
    std::vector<std::string> parents;
    std::map<std::string, std::string> files;
    std::string loaded_timestamp = "";

    std::istringstream iss(content);
    std::string line;
    bool in_message = false;

    while (std::getline(iss, line)) {
        if (line.empty() && !in_message) {
            in_message = true;
            continue;
        }
        if (in_message) {
            msg += line + "\n";
        } else {
            if (line.find("blob ") == 0) {
                std::istringstream s(line);
                std::string type, blob_hash, filename;
                s >> type >> blob_hash >> filename;
                files[filename] = blob_hash;
            } else if (line.find("parent ") == 0) {
                parents.push_back(line.substr(7));
            } else if (line.find("author ") == 0) {
                if (line.length() >= 26)
                    loaded_timestamp = line.substr(line.length() - 19);
            }
        }
    }
    if (!msg.empty() && msg[msg.size() - 1] == '\n') msg.pop_back();

    
    CommitNode c(msg, parents, files);
    c.hash = hash_from_file;
    if (!loaded_timestamp.empty()) c.timestamp = loaded_timestamp;

    std::cout << "[DEBUG] Deserialized commit:\n"
              << "Hash: " << c.hash << "\n"
             << "Message: " << c.message << "\n"
             << "Timestamp: " << c.timestamp << "\n"
             << "Parents count: " << c.parent_hashes.size() << "\n"
             << "Tracked files count: " << c.tracked_files.size() << "\n";



    return c;
}

class MiniGit {
private:
    std::map<std::string, std::string> staging_area; // filename -> hash
    std::vector<CommitNode> commit_history;
    std::string repo_dir = ".minigit";

    void load_commit_history() {
        commit_history.clear();

        std::ifstream head(repo_dir + "/HEAD");
        if (!head) return;

        std::string latest_hash;
        std::getline(head, latest_hash);
        head.close();

        std::string commit_path = repo_dir + "/" + latest_hash + ".txt";
        std::ifstream commit_file(commit_path);
        if (!commit_file) return;

        std::stringstream buffer;
        buffer << commit_file.rdbuf();
        std::string commit_content = buffer.str();

        CommitNode latest = CommitNode::deserialize(latest_hash, commit_content);
        commit_history.push_back(latest);
}


    void save_staging_area() {
        if (!file_exists(repo_dir)) {
            if (!create_directory(repo_dir)) {
                std::cout << "Error: Could not create repository directory.\n";
                return;
            }
        }

        std::string path = repo_dir + "/staging_area.txt";
        std::ofstream out(path);
        if (!out) {
            std::cout << "Error: Could not open staging area file for writing.\n";
            return;
        }

        for (const auto& kv : staging_area) {
            out << kv.first << " " << kv.second << "\n";
        }

        std::cout << "[DEBUG] Saved staging_area.txt with " << staging_area.size() << " entries.\n";
    }

    void load_staging_area() {
        staging_area.clear();
        std::ifstream in(repo_dir + "/staging_area.txt");
        if (!in) {
            std::cout << "[DEBUG] No staging_area.txt found.\n";
            return;
        }

        std::string filename, hash;
        while (in >> filename >> hash) {
            staging_area[filename] = hash;
        }

        std::cout << "[DEBUG] Loaded staging_area.txt with " << staging_area.size() << " entries.\n";
    }

public:
    MiniGit() {
        if (file_exists(repo_dir)) {
            load_staging_area();
            load_commit_history();
        }
    }

    void init() {
        if (create_directory(repo_dir)) {
            std::cout << "Initialized empty MiniGit repository in " << repo_dir << "\n";
        } else {
            std::cout << "Repository already exists or failed to create.\n";
        }
    }

    void add(const std::string& filename) {
        if (!file_exists(filename)) {
            std::cout << "Error: File '" << filename << "' does not exist.\n";
            return;
        }

        std::ifstream file(filename, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        std::string content_hash = generate_simple_hash(content);
        staging_area[filename] = content_hash;

        save_staging_area();

        std::cout << "Added file: " << filename << " with hash " << content_hash << "\n";
    }

    void commit(const std::string& message) {
        load_staging_area();  // <- Ensures we check updated staging file

        if (staging_area.empty()) {
            std::cout << "Nothing to commit. Staging area is empty.\n";
            return;
        }

        if (!file_exists(repo_dir)) {
            if (!create_directory(repo_dir)) {
                std::cout << "Error: Could not create repository directory.\n";
                return;
            }
        }

        std::vector<std::string> parents;

        CommitNode new_commit(message, parents, staging_area);
        std::string commit_data = new_commit.serialize();
        std::string commit_hash = generate_simple_hash(commit_data);
        new_commit.hash = commit_hash;

        std::string commit_path = repo_dir + "/" + commit_hash + ".txt";
        std::ofstream commit_file(commit_path);
        if (!commit_file) {
            std::cout << "Error: Could not write commit file.\n";
            return;
        }
        commit_file << commit_data;
        commit_file.close();
        commit_history.push_back(new_commit);

        staging_area.clear();
        save_staging_area();

        std::cout << "Committed with message: " << message << "\nCommit hash: " << commit_hash << "\n";
        std::ofstream head(repo_dir + "/HEAD");
        head << commit_hash;
        head.close();
    }

    void log() {
        if (commit_history.empty()) {
            std::cout << "No commits yet.\n";
            return;
        }

        for (auto it = commit_history.rbegin(); it != commit_history.rend(); ++it) {
            std::cout << "Commit: " << it->hash << "\n";
            std::cout << "Date: " << it->timestamp << "\n";
            std::cout << "Message: " << it->message << "\n\n";
        }
    }

    void branch(const std::string& name) {
        std::cout << "Created branch: " << name << "\n";
    }

    void checkout(const std::string& name) {
        std::cout << "Checked out: " << name << "\n";
    }

    void merge(const std::string& branch_name) {
        std::cout << "Merged branch: " << branch_name << "\n";
    }
};



int main(int argc, char* argv[]) {
    MiniGit minigit;

    if (argc < 2) {
        std::cout << "Usage: minigit <command> [args]\n";
        std::cout << "Commands:\n";
        std::cout << "  init                   Initialize a new MiniGit repository\n";
        std::cout << "  add <filename>         Add file to staging area\n";
        std::cout << "  commit <message>       Commit staged files with message\n";
        std::cout << "  log                    Show commit history\n";
        std::cout << "  branch <branch_name>   Create a new branch\n";
        std::cout << "  checkout <name>        Checkout branch or commit\n";
        std::cout << "  merge <branch_name>    Merge branch into current branch\n";
        return 1;
    }

    std::string command = argv[1];

    if (command == "init") {
        minigit.init();
    } 
    else if (command == "add") {
        if (argc < 3) {
            std::cerr << "Error: 'add' command requires a filename.\n";
            return 1;
        }
        minigit.add(argv[2]);
    }
    else if (command == "commit") {
        if (argc < 3) {
            std::cerr << "Error: 'commit' command requires a message.\n";
            return 1;
        }
        std::string message;
        for (int i = 2; i < argc; ++i) {
            if (i > 2) message += " ";
            message += argv[i];
        }
        minigit.commit(message);
    }
    else if (command == "log") {
        minigit.log();
    }
    else if (command == "branch") {
        if (argc < 3) {
            std::cerr << "Error: 'branch' command requires a branch name.\n";
            return 1;
        }
        minigit.branch(argv[2]);
    }
    else if (command == "checkout") {
        if (argc < 3) {
            std::cerr << "Error: 'checkout' command requires a name.\n";
            return 1;
        }
        minigit.checkout(argv[2]);
    }
    else if (command == "merge") {
        if (argc < 3) {
            std::cerr << "Error: 'merge' command requires a branch name.\n";
            return 1;
        }
        minigit.merge(argv[2]);
    }
    else {
        std::cerr << "Error: Unknown command '" << command << "'.\n";
        return 1;
    }

    return 0;
}  