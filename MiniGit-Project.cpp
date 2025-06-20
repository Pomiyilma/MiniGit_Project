#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem> // C++17 for directory ops
#include <chrono>     // For timestamps
#include <iomanip>    // For string formatting
#include <sstream>    // For string streams
#include <map>        // Maps for staging & commit files
#include <unordered_set>
#include <queue>

namespace fs = std::filesystem;

// Simple pseudo-hash: combines content + timestamp + standard hash
std::string generate_simple_hash(const std::string& content) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::hash<std::string> hasher;
    std::string data_to_hash = content + std::to_string(timestamp) + std::to_string(hasher(content));
    size_t content_hash = hasher(data_to_hash);

    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << content_hash; // 16 hex chars for hash look
    return ss.str();
}

class CommitNode {
public:
    std::string hash;
    std::string message;
    std::string timestamp;
    std::vector<std::string> parent_hashes; // Supports merges (1 or 2 parents)
    std::map<std::string, std::string> tracked_files; // filename -> blob hash

    CommitNode() = default;

    CommitNode(const std::string& msg,
               const std::vector<std::string>& parents,
               const std::map<std::string, std::string>& files_map)
        : message(msg), parent_hashes(parents), tracked_files(files_map)
    {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
        timestamp = ss.str();
    }

    // Serialize commit object similar to Git style
    std::string serialize() const {
        std::stringstream ss;
        ss << "tree\n"; // placeholder
        for (const auto& [filename, blob_hash] : tracked_files) {
            ss << "blob " << blob_hash << " " << filename << "\n";
        }
        for (const auto& p : parent_hashes) {
            ss << "parent " << p << "\n";
        }
        ss << "author MiniGit <minigit@example.com> " << timestamp << "\n";
        ss << "committer MiniGit <minigit@example.com> " << timestamp << "\n\n";
        ss << message << "\n";
        return ss.str();
    }

    static CommitNode deserialize(const std::string& hash_from_file, const std::string& content) {
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
                if (line.rfind("blob ", 0) == 0) {
                    std::istringstream s(line);
                    std::string type, blob_hash, filename;
                    s >> type >> blob_hash >> filename;
                    files[filename] = blob_hash;
                } else if (line.rfind("parent ", 0) == 0) {
                    parents.push_back(line.substr(7));
                } else if (line.rfind("author ", 0) == 0) {
                    if (line.length() >= 26)
                        loaded_timestamp = line.substr(line.length() - 19);
                }
            }
        }
        if (!msg.empty() && msg.back() == '\n') msg.pop_back();

        CommitNode c(msg, parents, files);
        c.hash = hash_from_file;
        if (!loaded_timestamp.empty()) c.timestamp = loaded_timestamp;
        return c;
    }
};

class MiniGit {
private:
    const std::string minigit_dir_ = ".minigit";
    std::map<std::string, std::string> staging_area_; // filename -> blob hash

    // Save content as blob and return hash
    std::string save_blob(const std::string& content) {
        std::string hash = generate_simple_hash(content);
        std::string blob_path = minigit_dir_ + "/objects/" + hash;
        std::ofstream ofs(blob_path);
        if (!ofs) {
            std::cerr << "Error: Cannot save blob.\n";
            return "";
        }
        ofs << content;
        return hash;
    }

    // Read blob content by hash
    std::string read_blob(const std::string& hash) {
        std::string blob_path = minigit_dir_ + "/objects/" + hash;
        std::ifstream ifs(blob_path);
        if (!ifs) {
            std::cerr << "Error: Cannot read blob " << hash << "\n";
            return "";
        }
        return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    // Save commit object
    void save_commit(const CommitNode& commit) {
        std::string commit_path = minigit_dir_ + "/objects/" + commit.hash;
        std::ofstream ofs(commit_path);
        if (!ofs) {
            std::cerr << "Error: Cannot save commit object.\n";
            return;
        }
        ofs << commit.serialize();
    }

    // Read commit object by hash
    CommitNode read_commit(const std::string& hash) {
        std::string commit_path = minigit_dir_ + "/objects/" + hash;
        std::ifstream ifs(commit_path);
        if (!ifs) return CommitNode(); // empty commit
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        return CommitNode::deserialize(hash, content);
    }

    // Get HEAD commit hash (from branch or detached)
    std::string get_head_hash() {
        std::ifstream ifs(minigit_dir_ + "/HEAD");
        if (!ifs) return "";
        std::string line;
        std::getline(ifs, line);
        if (line.rfind("ref: ", 0) == 0) {
            std::string ref = line.substr(5);
            std::ifstream ref_ifs(minigit_dir_ + "/" + ref);
            if (!ref_ifs) return "";
            std::string commit_hash;
            std::getline(ref_ifs, commit_hash);
            return commit_hash;
        }
        return line; // detached HEAD
    }

    // Update HEAD pointer
    void update_head(const std::string& new_hash) {
        std::ifstream ifs(minigit_dir_ + "/HEAD");
        if (!ifs) {
            std::cerr << "Error: HEAD file missing.\n";
            return;
        }
        std::string line;
        std::getline(ifs, line);
        ifs.close();

        if (line.rfind("ref: ", 0) == 0) {
            std::string ref = line.substr(5);
            std::ofstream ofs(minigit_dir_ + "/" + ref);
            if (!ofs) {
                std::cerr << "Error: Cannot update ref.\n";
                return;
            }
            ofs << new_hash << "\n";
        } else {
            std::ofstream ofs(minigit_dir_ + "/HEAD");
            if (!ofs) {
                std::cerr << "Error: Cannot update HEAD.\n";
                return;
            }
            ofs << new_hash << "\n";
        }
    }

    // Write working directory files from tracked_files map
    void checkout_files(const std::map<std::string, std::string>& files) {
        for (const auto& [filename, blob_hash] : files) {
            std::string content = read_blob(blob_hash);
            std::ofstream ofs(filename);
            ofs << content;
        }
    }

    // Find Lowest Common Ancestor commit for merge (BFS backward)
    std::string find_lca(const std::string& hash1, const std::string& hash2) {
        if (hash1.empty() || hash2.empty()) return "";

        std::unordered_set<std::string> visited;
        std::queue<std::string> q;

        q.push(hash1);
        while (!q.empty()) {
            auto cur = q.front(); q.pop();
            visited.insert(cur);
            CommitNode c = read_commit(cur);
            for (const auto& p : c.parent_hashes)
                if (!p.empty() && visited.find(p) == visited.end())
                    q.push(p);
        }

        q.push(hash2);
        while (!q.empty()) {
            auto cur = q.front(); q.pop();
            if (visited.find(cur) != visited.end())
                return cur;
            CommitNode c = read_commit(cur);
            for (const auto& p : c.parent_hashes)
                if (!p.empty())
                    q.push(p);
        }

        return ""; // no common ancestor
    }

    // Merge helper: write conflict markers to conflicted files
    void write_conflict_file(const std::string& filename, const std::string& ours, const std::string& theirs) {
        std::ofstream ofs(filename);
        ofs << "<<<<<<< OURS\n" << ours << "\n=======\n" << theirs << "\n>>>>>>> THEIRS\n";
    }

    // Merge 3-way logic for files, returns merged file map and flags if conflict occurs
    std::pair<std::map<std::string, std::string>, bool> merge_file_maps(
        const std::map<std::string, std::string>& base,
        const std::map<std::string, std::string>& ours,
        const std::map<std::string, std::string>& theirs)
    {
        std::map<std::string, std::string> merged;
        bool has_conflict = false;
        std::unordered_set<std::string> all_files;

        for (auto& [f, _] : base) all_files.insert(f);
        for (auto& [f, _] : ours) all_files.insert(f);
        for (auto& [f, _] : theirs) all_files.insert(f);

        for (const auto& f : all_files) {
            std::string base_hash = base.count(f) ? base.at(f) : "";
            std::string our_hash = ours.count(f) ? ours.at(f) : "";
            std::string their_hash = theirs.count(f) ? theirs.at(f) : "";

            if (our_hash == their_hash) {
                merged[f] = our_hash;
            } else if (base_hash == our_hash) {
                merged[f] = their_hash;
            } else if (base_hash == their_hash) {
                merged[f] = our_hash;
            } else {
                // Conflict
                has_conflict = true;
                merged[f] = ""; // placeholder, real file will contain conflict markers

                // Load contents or empty string if missing
                std::string our_content = our_hash.empty() ? "" : read_blob(our_hash);
                std::string their_content = their_hash.empty() ? "" : read_blob(their_hash);
                write_conflict_file(f, our_content, their_content);
            }
        }

        return {merged, has_conflict};
    }

public:
    void init() {
        if (fs::exists(minigit_dir_)) {
            std::cout << "MiniGit repository already exists.\n";
            return;
        }

        fs::create_directory(minigit_dir_);
        fs::create_directory(minigit_dir_ + "/objects");
        fs::create_directory(minigit_dir_ + "/refs");
        fs::create_directory(minigit_dir_ + "/refs/heads");

        // Create HEAD pointing to main branch
        std::ofstream ofs(minigit_dir_ + "/HEAD");
        ofs << "ref: refs/heads/main\n";

        // Create empty main branch ref
        std::ofstream ofs_main(minigit_dir_ + "/refs/heads/main");
        ofs_main.close();

        std::cout << "Initialized empty MiniGit repository in " << minigit_dir_ << "\n";
    }

    void add(const std::string& filename) {
        if (!fs::exists(filename)) {
            std::cerr << "Error: File not found: " << filename << "\n";
            return;
        }
        std::ifstream ifs(filename);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        std::string blob_hash = save_blob(content);
        if (!blob_hash.empty()) {
            staging_area_[filename] = blob_hash;
            std::cout << "Staged " << filename << " with blob hash " << blob_hash << "\n";
        }
    }

    void commit(const std::string& message) {
        if (staging_area_.empty()) {
            std::cout << "Nothing to commit.\n";
            return;
        }

        std::string parent_hash = get_head_hash();
        std::vector<std::string> parents;
        if (!parent_hash.empty()) parents.push_back(parent_hash);

        CommitNode new_commit(message, parents, staging_area_);
        new_commit.hash = generate_simple_hash(new_commit.serialize());

        save_commit(new_commit);
        update_head(new_commit.hash);
        staging_area_.clear();

        std::cout << "Committed: " << new_commit.hash.substr(0, 7) << " " << message << "\n";
    }

    void log() {
        std::string current = get_head_hash();
        if (current.empty()) {
            std::cout << "No commits yet.\n";
            return;
        }

        while (!current.empty()) {
            CommitNode c = read_commit(current);
            if (c.hash.empty()) break;

            std::cout << "commit " << c.hash << "\n";
            std::cout << "Date: " << c.timestamp << "\n\n";
            std::cout << "    " << c.message << "\n\n";

            if (c.parent_hashes.empty()) break;
            current = c.parent_hashes[0];
        }
    }

    void branch(const std::string& branch_name) {
        std::string branch_path = minigit_dir_ + "/refs/heads/" + branch_name;
        if (fs::exists(branch_path)) {
            std::cout << "Branch '" << branch_name << "' already exists. Updating it.\n";
        }

        std::string head_hash = get_head_hash();
        if (head_hash.empty()) {
            std::cerr << "No commits yet. Commit before creating branches.\n";
            return;
        }

        std::ofstream ofs(branch_path);
        ofs << head_hash << "\n";
        std::cout << "Branch '" << branch_name << "' created at " << head_hash.substr(0, 7) << "\n";
    }

    void checkout(const std::string& name) {
        std::string target_commit_hash;

        // Check if name is a branch
        std::string branch_path = minigit_dir_ + "/refs/heads/" + name;
        if (fs::exists(branch_path)) {
            std::ifstream ifs(branch_path);
            std::getline(ifs, target_commit_hash);
            if (target_commit_hash.empty()) {
                std::cerr << "Branch " << name << " has no commits yet.\n";
                return;
            }
            // Update HEAD to point to this branch
            std::ofstream head_ofs(minigit_dir_ + "/HEAD");
            head_ofs << "ref: refs/heads/" << name << "\n";
        } else {
            // Assume name is a commit hash
            // Check if commit exists
            if (!fs::exists(minigit_dir_ + "/objects/" + name)) {
                std::cerr << "Commit or branch '" << name << "' does not exist.\n";
                return;
            }
            target_commit_hash = name;
            // Detached HEAD: directly point to commit hash
            std::ofstream head_ofs(minigit_dir_ + "/HEAD");
            head_ofs << target_commit_hash << "\n";
        }

        CommitNode commit = read_commit(target_commit_hash);
        if (commit.hash.empty()) {
            std::cerr << "Failed to read commit " << target_commit_hash << "\n";
            return;
        }

        // Overwrite working directory files to commit state
        checkout_files(commit.tracked_files);

        std::cout << "Checked out '" << name << "' at commit " << target_commit_hash.substr(0, 7) << "\n";
    }

    void merge(const std::string& branch_name) {
        // Validate branch exists
        std::string branch_path = minigit_dir_ + "/refs/heads/" + branch_name;
        if (!fs::exists(branch_path)) {
            std::cerr << "Branch '" << branch_name << "' does not exist.\n";
            return;
        }

        std::string current_hash = get_head_hash();
        if (current_hash.empty()) {
            std::cerr << "No commits to merge into.\n";
            return;
        }

        std::ifstream ifs(branch_path);
        std::string their_hash;
        std::getline(ifs, their_hash);

        std::string lca_hash = find_lca(current_hash, their_hash);
        if (lca_hash.empty()) {
            std::cerr << "No common ancestor found. Cannot merge.\n";
            return;
        }

        CommitNode base_commit = read_commit(lca_hash);
        CommitNode our_commit = read_commit(current_hash);
        CommitNode their_commit = read_commit(their_hash);

        auto [merged_map, conflict] = merge_file_maps(base_commit.tracked_files,
                                                     our_commit.tracked_files,
                                                     their_commit.tracked_files);

        if (conflict) {
            std::cout << "Merge has conflicts! Resolve them manually.\n";
        } else {
            std::cout << "Merge completed without conflicts.\n";
        }

        // Create merge commit with two parents
        std::vector<std::string> parents{current_hash, their_hash};
        CommitNode merge_commit("Merge branch '" + branch_name + "'", parents, merged_map);
        merge_commit.hash = generate_simple_hash(merge_commit.serialize());

        save_commit(merge_commit);
        update_head(merge_commit.hash);

        // Update working directory to merged files
        checkout_files(merged_map);

        std::cout << "Merge commit created: " << merge_commit.hash.substr(0, 7) << "\n";
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
        std::string filename = argv[2];
        minigit.add(filename);
    }
    else if (command == "commit") {
        if (argc < 3) {
            std::cerr << "Error: 'commit' command requires a commit message.\n";
            return 1;
        }
        // Join all remaining args as commit message
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
        std::string branch_name = argv[2];
        minigit.branch(branch_name);
    }
    else if (command == "checkout") {
        if (argc < 3) {
            std::cerr << "Error: 'checkout' command requires a branch name or commit hash.\n";
            return 1;
        }
        std::string name = argv[2];
        minigit.checkout(name);
    }
    else if (command == "merge") {
        if (argc < 3) {
            std::cerr << "Error: 'merge' command requires a branch name.\n";
            return 1;
        }
        std::string branch_name = argv[2];
        minigit.merge(branch_name);
    }
    else {
        std::cerr << "Error: Unknown command '" << command << "'.\n";
        return 1;
    }

    return 0;
}

