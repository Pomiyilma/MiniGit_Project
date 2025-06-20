#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem> // C++17 for directory operations
#include <chrono>     // For timestamp
#include <iomanip>    // For string formatting
#include <sstream>    // For string streams
#include <map>        // For std::map used in CommitNode and staging_area_
#include <unordered_set> // For unordered_set, used for staging and ancestor tracking
#include <unordered_map> // For unordered_map (though not explicitly used in final merged code, good for general utility)
#include <ctime>      // For ctime (time management)
#include <openssl/sha.h> // For SHA-1 hashing

namespace fs = std::filesystem;

// ======= Helper Functions =======

// SHA-1 hashing function
std::string computeSHA1(const std::string& content) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(content.c_str()), content.length(), hash);

    std::stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// Converts a timestamp to a readable string format
std::string format_timestamp(std::time_t timestamp) {
    std::tm* ptm = std::localtime(&timestamp);
    std::ostringstream oss;
    oss << std::put_time(ptm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// ======= CommitNode Class =======

class CommitNode {
public:
    std::string hash;
    std::string message;
    std::string timestamp;
    std::string parent_hash; // For single parent
    std::vector<std::string> parent_hashes; // For merges, can contain one or two hashes
    std::map<std::string, std::string> tracked_files; // filename -> blob hash

    CommitNode(const std::string& msg, const std::string& parent_h, const std::map<std::string, std::string>& files_map)
        : message(msg), parent_hash(parent_h), tracked_files(files_map) {
        std::time_t now = std::time(nullptr);
        timestamp = format_timestamp(now);

        // For simplicity, populate parent_hashes based on parent_h
        if (!parent_h.empty()) {
            parent_hashes.push_back(parent_h);
        }

        // Calculate hash after all fields are set
        calculate_hash();
    }

    // Constructor for deserialization
    CommitNode(const std::string& h, const std::string& msg, const std::string& ts, const std::string& p_h,
               const std::vector<std::string>& p_hs, const std::map<std::string, std::string>& files_map)
        : hash(h), message(msg), timestamp(ts), parent_hash(p_h), parent_hashes(p_hs), tracked_files(files_map) {}

    void add_parent(const std::string& parent_h) {
        parent_hashes.push_back(parent_h);
        // If there was only one parent, update parent_hash to the first one for consistency
        if (parent_hashes.size() == 1) {
            parent_hash = parent_hashes[0];
        }
    }

private:
    void calculate_hash() {
        std::stringstream ss;
        ss << message << timestamp << parent_hash;
        for (const auto& p_h : parent_hashes) {
            ss << p_h;
        }
        for (const auto& pair : tracked_files) {
            ss << pair.first << pair.second;
        }
        hash = computeSHA1(ss.str());
    }
};

// ======= MiniGit Class =======

class MiniGit {
public:
    MiniGit() : minigit_dir_name_(".minigit") {
        // Load staging area if .minigit directory exists
        if (fs::exists(minigit_dir_name_)) {
            load_staging_area(); //
        }
    }

    void init() {
        if (fs::exists(minigit_dir_name_)) {
            std::cout << "MiniGit repository already initialized." << std::endl;
            return;
        }
        fs::create_directory(minigit_dir_name_);
        fs::create_directory(fs::path(minigit_dir_name_) / "objects");
        fs::create_directory(fs::path(minigit_dir_name_) / "commits");
        fs::create_directory(fs::path(minigit_dir_name_) / "refs");
        std::ofstream head_file(fs::path(minigit_dir_name_) / "refs" / "HEAD");
        head_file << "ref: refs/heads/master\n"; // Point HEAD to master branch
        head_file.close();
        
        save_staging_area(); // Create an empty index file on init
        std::cout << "Initialized empty MiniGit repository in " << minigit_dir_name_ << std::endl;
    }

    void add(const std::string& filename) {
        if (!fs::exists(filename)) {
            std::cerr << "Error: File '" << filename << "' not found." << std::endl;
            return;
        }

        std::ifstream ifs(filename, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        std::string blob_hash = save_blob(content);
        if (!blob_hash.empty()) {
            staging_area_[filename] = blob_hash;
            std::cout << "Added '" << filename << "' to staging area. Blob hash: " << blob_hash << std::endl;
            save_staging_area(); // Persist the updated staging area
        } else {
            std::cerr << "Error: Failed to save blob for " << filename << std::endl;
        }
    }

    void commit(const std::string& message) {
        if (staging_area_.empty()) {
            std::cout << "Nothing to commit, working tree clean." << std::endl;
            return;
        }

        std::string parent_hash = get_head_commit_hash();
        // If parent_hash is "ref: refs/heads/master", it means master branch exists but no commits yet
        if (parent_hash.rfind("ref: ", 0) == 0) { // Check if it starts with "ref: "
            // This is the initial commit for the branch, so no parent hash
            parent_hash = "";
        }

        CommitNode new_commit(message, parent_hash, staging_area_);
        save_commit_object(new_commit);

        update_head(new_commit.hash);

        std::cout << "Committed: " << message << std::endl;
        std::cout << "Commit Hash: " << new_commit.hash << std::endl;

        staging_area_.clear(); // Clear in-memory staging area after commit
        save_staging_area(); // Persist the empty staging area
    }

    void log() {
        std::string current_commit_hash = get_head_commit_hash();
        if (current_commit_hash.rfind("ref: ", 0) == 0) {
            std::string branch_path_str = fs::path(minigit_dir_name_) / current_commit_hash.substr(5);
            if (fs::exists(branch_path_str)) {
                std::ifstream branch_file(branch_path_str);
                std::getline(branch_file, current_commit_hash);
            } else {
                std::cout << "No commits yet on this branch." << std::endl;
                return;
            }
        }

        if (current_commit_hash.empty()) {
            std::cout << "No commits yet." << std::endl;
            return;
        }

        while (!current_commit_hash.empty()) {
            CommitNode commit = read_commit_object(current_commit_hash);
            if (commit.hash.empty()) { // Check if commit was successfully read
                break;
            }
            std::cout << "commit " << commit.hash << std::endl;
            std::cout << "Date: " << commit.timestamp << std::endl;
            std::cout << "    " << commit.message << std::endl << std::endl;

            if (commit.parent_hashes.empty()) {
                break;
            } else if (commit.parent_hashes.size() == 1) {
                current_commit_hash = commit.parent_hashes[0];
            } else { // Merge commit, typically show first parent or handle specially
                current_commit_hash = commit.parent_hashes[0]; // Follow the first parent for linear log
                // You could extend this to show all parents or branch history visually
            }
        }
    }

    void branch(const std::string& branch_name) {
        fs::path branch_path = fs::path(minigit_dir_name_) / "refs" / "heads" / branch_name;
        if (fs::exists(branch_path)) {
            std::cout << "Branch '" << branch_name << "' already exists." << std::endl;
            return;
        }

        std::string head_commit_hash = get_head_commit_hash();
        if (head_commit_hash.rfind("ref: ", 0) == 0) { // If HEAD points to a ref, get the actual commit hash
            std::string actual_branch_path_str = fs::path(minigit_dir_name_) / head_commit_hash.substr(5);
            if (fs::exists(actual_branch_path_str)) {
                std::ifstream branch_file(actual_branch_path_str);
                std::getline(branch_file, head_commit_hash);
            } else {
                head_commit_hash = ""; // No actual commit yet
            }
        }

        if (head_commit_hash.empty()) {
            std::cout << "Cannot create branch: No commits yet." << std::endl;
            return;
        }

        std::ofstream branch_file(branch_path);
        branch_file << head_commit_hash << "\n";
        branch_file.close();
        std::cout << "Branch '" << branch_name << "' created at " << head_commit_hash << std::endl;
    }

    void checkout(const std::string& target_ref) {
        std::string commit_hash_to_checkout = "";
        fs::path branch_path = fs::path(minigit_dir_name_) / "refs" / "heads" / target_ref;

        if (fs::exists(branch_path)) {
            // It's a branch name
            std::ifstream ifs(branch_path);
            std::getline(ifs, commit_hash_to_checkout);
            ifs.close();
            // Update HEAD to point to this branch
            std::ofstream head_file(fs::path(minigit_dir_name_) / "refs" / "HEAD");
            head_file << "ref: refs/heads/" << target_ref << "\n";
            head_file.close();
            std::cout << "Switched to branch '" << target_ref << "'" << std::endl;
        } else if (target_ref.length() == SHA_DIGEST_LENGTH * 2 && fs::exists(fs::path(minigit_dir_name_) / "commits" / target_ref)) {
            // It's a commit hash
            commit_hash_to_checkout = target_ref;
            // Detach HEAD
            std::ofstream head_file(fs::path(minigit_dir_name_) / "refs" / "HEAD");
            head_file << commit_hash_to_checkout << "\n"; // HEAD now points directly to commit hash
            head_file.close();
            std::cout << "Switched to commit '" << target_ref << "' (detached HEAD)" << std::endl;
        } else {
            std::cerr << "Error: Reference '" << target_ref << "' not found." << std::endl;
            return;
        }

        // Restore working directory to the state of the checked-out commit
        if (!commit_hash_to_checkout.empty()) {
            // Clear current working directory files (excluding .minigit)
            for (const auto& entry : fs::directory_iterator(".")) {
                if (entry.path().filename() != minigit_dir_name_) {
                    fs::remove_all(entry.path());
                }
            }

            // Load files from the checked-out commit
            CommitNode commit = read_commit_object(commit_hash_to_checkout);
            if (!commit.hash.empty()) {
                for (const auto& file_pair : commit.tracked_files) {
                    std::string filename = file_pair.first;
                    std::string blob_hash = file_pair.second;
                    std::string content = read_blob(blob_hash);
                    std::ofstream ofs(filename, std::ios::binary);
                    if (ofs.is_open()) {
                        ofs << content;
                        ofs.close();
                    } else {
                        std::cerr << "Warning: Could not restore file " << filename << std::endl;
                    }
                }
            } else {
                std::cerr << "Error: Could not read commit object for checkout." << std::endl;
            }
        }
    }


    void merge(const std::string& branch_name) {
        std::string current_branch_head_hash = get_head_commit_hash();
        if (current_branch_head_hash.rfind("ref: ", 0) == 0) {
            std::string actual_branch_path_str = fs::path(minigit_dir_name_) / current_branch_head_hash.substr(5);
            std::ifstream branch_file(actual_branch_path_str);
            std::getline(branch_file, current_branch_head_hash);
        }

        fs::path target_branch_path = fs::path(minigit_dir_name_) / "refs" / "heads" / branch_name;
        if (!fs::exists(target_branch_path)) {
            std::cerr << "Error: Branch '" << branch_name << "' does not exist." << std::endl;
            return;
        }
        std::ifstream target_branch_file(target_branch_path);
        std::string target_branch_head_hash;
        std::getline(target_branch_file, target_branch_head_hash);
        target_branch_file.close();

        if (current_branch_head_hash == target_branch_head_hash) {
            std::cout << "Already up to date." << std::endl;
            return;
        }

        // Find common ancestor
        CommitNode common_ancestor = find_common_ancestor(current_branch_head_hash, target_branch_head_hash);
        if (common_ancestor.hash.empty()) {
            std::cerr << "Error: Could not find common ancestor." << std::endl;
            return;
        }

        CommitNode current_head_commit = read_commit_object(current_branch_head_hash);
        CommitNode target_head_commit = read_commit_object(target_branch_head_hash);

        std::map<std::string, std::string> merged_files = current_head_commit.tracked_files;
        bool conflict = false;

        // Iterate through files in the target branch
        for (const auto& target_file_pair : target_head_commit.tracked_files) {
            const std::string& filename = target_file_pair.first;
            const std::string& target_blob_hash = target_file_pair.second;
            std::string current_blob_hash = current_head_commit.tracked_files.count(filename) ? current_head_commit.tracked_files.at(filename) : "";
            std::string ancestor_blob_hash = common_ancestor.tracked_files.count(filename) ? common_ancestor.tracked_files.at(filename) : "";

            if (current_blob_hash.empty() && ancestor_blob_hash.empty()) {
                // File added in target branch, not in current or ancestor
                merged_files[filename] = target_blob_hash;
            } else if (current_blob_hash.empty() && !ancestor_blob_hash.empty()) {
                // File deleted in current branch but present in ancestor and target
                // This is a conflict if target modified it
                if (target_blob_hash != ancestor_blob_hash) {
                    std::cerr << "CONFLICT (delete/modify): " << filename << " deleted in current, modified in " << branch_name << std::endl;
                    conflict = true;
                    // Add conflict markers to the file in the working directory
                    add_conflict_markers(filename, read_blob(current_blob_hash), read_blob(target_blob_hash));
                } else {
                    // File deleted in current, not modified in target from ancestor -> assume delete
                    merged_files.erase(filename);
                }
            } else if (!current_blob_hash.empty() && ancestor_blob_hash.empty()) {
                // File added in current branch
                // If also added in target with different content, it's a conflict
                if (target_head_commit.tracked_files.count(filename) && target_blob_hash != current_blob_hash) {
                    std::cerr << "CONFLICT (add/add): " << filename << " added differently" << std::endl;
                    conflict = true;
                    add_conflict_markers(filename, read_blob(current_blob_hash), read_blob(target_blob_hash));
                } else if (!target_head_commit.tracked_files.count(filename)) {
                    // Only added in current, keep it
                    // No change to merged_files[filename] as it's already from current_head_commit
                }
            } else { // File exists in current, target, and ancestor
                if (current_blob_hash == target_blob_hash) {
                    // No change or same change
                    // merged_files[filename] is already correct (from current_head_commit)
                } else if (current_blob_hash == ancestor_blob_hash) {
                    // Changed in target, not in current -> fast-forward file in current
                    merged_files[filename] = target_blob_hash;
                } else if (target_blob_hash == ancestor_blob_hash) {
                    // Changed in current, not in target -> keep current
                    // merged_files[filename] is already correct
                } else {
                    // Modified in both current and target, and different from ancestor -> conflict
                    std::cerr << "CONFLICT (content): " << filename << std::endl;
                    conflict = true;
                    add_conflict_markers(filename, read_blob(current_blob_hash), read_blob(target_blob_hash));
                }
            }
        }

        // Handle files deleted in target branch but present in current and ancestor
        for (const auto& current_file_pair : current_head_commit.tracked_files) {
            const std::string& filename = current_file_pair.first;
            const std::string& current_blob_hash = current_file_pair.second;
            std::string target_blob_hash = target_head_commit.tracked_files.count(filename) ? target_head_commit.tracked_files.at(filename) : "";
            std::string ancestor_blob_hash = common_ancestor.tracked_files.count(filename) ? common_ancestor.tracked_files.at(filename) : "";

            if (!target_head_commit.tracked_files.count(filename) && !ancestor_blob_hash.empty() && current_blob_hash == ancestor_blob_hash) {
                // File deleted in target, unchanged in current from ancestor
                // -> delete it in merged_files
                merged_files.erase(filename);
            } else if (!target_head_commit.tracked_files.count(filename) && !ancestor_blob_hash.empty() && current_blob_hash != ancestor_blob_hash) {
                // File deleted in target, but modified in current from ancestor
                std::cerr << "CONFLICT (delete/modify): " << filename << " deleted in " << branch_name << ", modified in current" << std::endl;
                conflict = true;
                add_conflict_markers(filename, read_blob(current_blob_hash), read_blob(target_blob_hash)); // target_blob_hash would be empty for deleted file
            }
        }


        if (conflict) {
            std::cout << "Automatic merge failed; fix conflicts and then commit." << std::endl;
            // Staging area should contain conflicted files with their conflict markers
            // For simplicity, we just leave the files in the working directory with markers
            // and the user would manually add/commit after resolving.
            // For a more complete MiniGit, you would update the staging_area_
            // to reflect the conflicted state (e.g., add special entries for conflict).
        } else {
            // Update working directory to merged state
            // First, clear all non-minigit files
            for (const auto& entry : fs::directory_iterator(".")) {
                if (entry.path().filename() != minigit_dir_name_) {
                    fs::remove_all(entry.path());
                }
            }

            // Write merged files to working directory
            for (const auto& file_pair : merged_files) {
                std::string filename = file_pair.first;
                std::string blob_hash = file_pair.second;
                std::string content = read_blob(blob_hash);
                std::ofstream ofs(filename, std::ios::binary);
                if (ofs.is_open()) {
                    ofs << content;
                    ofs.close();
                } else {
                    std::cerr << "Warning: Could not write merged file " << filename << std::endl;
                }
            }

            // Create a new merge commit
            // The message for a merge commit usually combines the branches
            std::string merge_message = "Merge branch '" + branch_name + "' into " + get_current_branch_name();
            CommitNode merge_commit(merge_message, "", merged_files); // Parent_hash will be set by add_parent
            merge_commit.add_parent(current_branch_head_hash);
            merge_commit.add_parent(target_branch_head_hash);
            save_commit_object(merge_commit);
            update_head(merge_commit.hash);
            staging_area_.clear(); // Clear staging after successful merge commit
            save_staging_area(); // Persist empty staging area

            std::cout << "Merge successful. New commit: " << merge_commit.hash << std::endl;
        }
    }


private:
    std::string minigit_dir_name_;
    std::map<std::string, std::string> staging_area_;

    // New private helper methods for staging area persistence
    void save_staging_area(); //
    void load_staging_area(); //

    std::string save_blob(const std::string& content) {
        std::string hash = computeSHA1(content);
        fs::path blob_path = fs::path(minigit_dir_name_) / "objects" / hash;
        if (!fs::exists(blob_path)) {
            std::ofstream ofs(blob_path, std::ios::binary);
            if (ofs.is_open()) {
                ofs << content;
                ofs.close();
            } else {
                std::cerr << "Error: Could not save blob to " << blob_path << std::endl;
                return "";
            }
        }
        return hash;
    }

    std::string read_blob(const std::string& hash) {
        fs::path blob_path = fs::path(minigit_dir_name_) / "objects" / hash;
        if (!fs::exists(blob_path)) {
            std::cerr << "Error: Blob object '" << hash << "' not found." << std::endl;
            return "";
        }
        std::ifstream ifs(blob_path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();
        return content;
    }

    void save_commit_object(const CommitNode& commit) {
        fs::path commit_path = fs::path(minigit_dir_name_) / "commits" / commit.hash;
        std::ofstream ofs(commit_path);
        if (ofs.is_open()) {
            ofs << "hash:" << commit.hash << "\n";
            ofs << "message:" << commit.message << "\n";
            ofs << "timestamp:" << commit.timestamp << "\n";
            ofs << "parent_hash:";
            for (size_t i = 0; i < commit.parent_hashes.size(); ++i) {
                ofs << commit.parent_hashes[i] << (i == commit.parent_hashes.size() - 1 ? "" : ",");
            }
            ofs << "\n";
            for (const auto& pair : commit.tracked_files) {
                ofs << "file:" << pair.first << ":" << pair.second << "\n";
            }
            ofs.close();
        } else {
            std::cerr << "Error: Could not save commit object to " << commit_path << std::endl;
        }
    }

    CommitNode read_commit_object(const std::string& hash) {
        fs::path commit_path = fs::path(minigit_dir_name_) / "commits" / hash;
        if (!fs::exists(commit_path)) {
            // std::cerr << "Error: Commit object '" << hash << "' not found." << std::endl;
            return CommitNode("", "", {}); // Return an empty/invalid commit
        }
        std::ifstream ifs(commit_path);
        std::string line;
        std::string msg, ts, p_h_str, commit_hash_from_file;
        std::vector<std::string> p_hs;
        std::map<std::string, std::string> files_map;

        while (std::getline(ifs, line)) {
            if (line.rfind("hash:", 0) == 0) {
                commit_hash_from_file = line.substr(5);
            } else if (line.rfind("message:", 0) == 0) {
                msg = line.substr(8);
            } else if (line.rfind("timestamp:", 0) == 0) {
                ts = line.substr(10);
            } else if (line.rfind("parent_hash:", 0) == 0) {
                std::string parents_str = line.substr(12);
                std::stringstream ss(parents_str);
                std::string p_hash;
                while (std::getline(ss, p_hash, ',')) {
                    p_hs.push_back(p_hash);
                }
                if (!p_hs.empty()) {
                    p_h_str = p_hs[0]; // Set parent_hash to the first parent
                }
            } else if (line.rfind("file:", 0) == 0) {
                std::string file_info = line.substr(5);
                size_t first_colon = file_info.find(':');
                if (first_colon != std::string::npos) {
                    std::string filename = file_info.substr(0, first_colon);
                    std::string blob_hash = file_info.substr(first_colon + 1);
                    files_map[filename] = blob_hash;
                }
            }
        }
        ifs.close();
        return CommitNode(commit_hash_from_file, msg, ts, p_h_str, p_hs, files_map);
    }

    std::string get_head_commit_hash() {
        fs::path head_path = fs::path(minigit_dir_name_) / "refs" / "HEAD";
        if (!fs::exists(head_path)) {
            return ""; // No HEAD file means no commits or uninitialized repo
        }
        std::ifstream ifs(head_path);
        std::string head_content;
        std::getline(ifs, head_content);
        ifs.close();

        if (head_content.rfind("ref: ", 0) == 0) {
            // HEAD points to a branch, e.g., "ref: refs/heads/master"
            std::string ref_path_str = fs::path(minigit_dir_name_) / head_content.substr(5);
            if (fs::exists(ref_path_str)) {
                std::ifstream ref_file(ref_path_str);
                std::string commit_hash;
                std::getline(ref_file, commit_hash);
                ref_file.close();
                return commit_hash; // Return the actual commit hash
            } else {
                return head_content; // Return the ref string if the branch file doesn't exist (e.g., initial state)
            }
        } else {
            // HEAD points directly to a commit hash (detached HEAD)
            return head_content;
        }
    }

    void update_head(const std::string& hash) {
        fs::path head_path = fs::path(minigit_dir_name_) / "refs" / "HEAD";
        std::string head_content;
        std::ifstream ifs(head_path);
        if (ifs.is_open()) {
            std::getline(ifs, head_content);
            ifs.close();
        }

        if (head_content.rfind("ref: refs/heads/", 0) == 0) {
            // HEAD is on a branch, update the branch pointer
            std::string branch_name = head_content.substr(std::string("ref: refs/heads/").length());
            fs::path branch_path = fs::path(minigit_dir_name_) / "refs" / "heads" / branch_name;
            std::ofstream ofs(branch_path);
            ofs << hash << "\n";
            ofs.close();
        } else {
            // Detached HEAD, simply update the HEAD file
            std::ofstream ofs(head_path);
            ofs << hash << "\n";
            ofs.close();
        }
    }

    std::string read_file(const std::string& path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            return ""; // File not found or couldn't be opened
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();
        return content;
    }

    // Merging helper: get all ancestors of a commit
    std::unordered_set<std::string> getAncestors(const std::string& hash) {
        std::unordered_set<std::string> ancestors;
        std::vector<std::string> q;
        if (!hash.empty()) {
            q.push_back(hash);
            ancestors.insert(hash);
        }

        while (!q.empty()) {
            std::string current_hash = q.back();
            q.pop_back();

            CommitNode commit = read_commit_object(current_hash);
            if (commit.hash.empty()) { // Error reading commit
                continue;
            }

            for (const std::string& parent : commit.parent_hashes) {
                if (ancestors.find(parent) == ancestors.end()) {
                    ancestors.insert(parent);
                    q.push_back(parent);
                }
            }
        }
        return ancestors;
    }

    // Merging helper: find the common ancestor between two commits
    CommitNode find_common_ancestor(const std::string& hash1, const std::string& hash2) {
        std::unordered_set<std::string> ancestors1 = getAncestors(hash1);
        std::unordered_set<std::string> ancestors2 = getAncestors(hash2);

        // Find intersection of ancestors
        std::string common_ancestor_hash = "";
        // To find the *latest* common ancestor, we can traverse from the heads
        // and find the first one that is an ancestor of both.
        // For simplicity, this finds *any* common ancestor. A more robust solution
        // would involve a reverse BFS or topological sort.
        for (const std::string& anc : ancestors1) {
            if (ancestors2.count(anc)) {
                // For a true merge base, you'd want the "deepest" common ancestor
                // This simple approach just finds the first one.
                // A better approach would involve tracking depths or doing a BFS from both sides.
                return read_commit_object(anc);
            }
        }
        return CommitNode("", "", {}); // No common ancestor found or error
    }

    void add_conflict_markers(const std::string& filename, const std::string& current_content, const std::string& incoming_content) {
        std::ofstream ofs(filename);
        if (ofs.is_open()) {
            ofs << "<<<<<<< HEAD\n";
            ofs << current_content;
            ofs << "=======\n";
            ofs << incoming_content;
            ofs << ">>>>>>> incoming\n";
            ofs.close();
            std::cout << "Conflict in " << filename << ". Markers added to file." << std::endl;
        } else {
            std::cerr << "Error: Could not write conflict markers to " << filename << std::endl;
        }
    }

    std::string get_current_branch_name() {
        fs::path head_path = fs::path(minigit_dir_name_) / "refs" / "HEAD";
        if (!fs::exists(head_path)) {
            return "master"; // Default if no HEAD file
        }
        std::ifstream ifs(head_path);
        std::string head_content;
        std::getline(ifs, head_content);
        ifs.close();

        if (head_content.rfind("ref: refs/heads/", 0) == 0) {
            return head_content.substr(std::string("ref: refs/heads/").length());
        } else {
            return "detached HEAD"; // If HEAD points directly to a commit
        }
    }

};

// Implementation of staging area persistence methods
void MiniGit::save_staging_area() {
    fs::path index_path = fs::path(minigit_dir_name_) / "index";
    std::ofstream ofs(index_path);
    if (ofs.is_open()) {
        for (const auto& pair : staging_area_) {
            ofs << pair.first << " " << pair.second << "\n"; // filename hash
        }
        ofs.close();
    } else {
        std::cerr << "Error: Could not save staging area to " << index_path << std::endl;
    }
}

void MiniGit::load_staging_area() {
    fs::path index_path = fs::path(minigit_dir_name_) / "index";
    staging_area_.clear(); // Clear existing in-memory state
    if (fs::exists(index_path)) {
        std::ifstream ifs(index_path);
        if (ifs.is_open()) {
            std::string filename, hash;
            while (ifs >> filename >> hash) {
                staging_area_[filename] = hash;
            }
            ifs.close();
        } else {
            std::cerr << "Error: Could not load staging area from " << index_path << std::endl;
        }
    }
}

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
    else if (cmd == "commit" && argc >= 3 && std::string(argv[2]) == "-m" && argc >= 4) minigit.commit(argv[3]);
    else if (cmd == "log") minigit.log();
    else if (cmd == "branch" && argc >= 3) minigit.branch(argv[2]);
    else if (cmd == "checkout" && argc >= 3) minigit.checkout(argv[2]);
    else if (cmd == "merge" && argc >= 3) minigit.merge(argv[2]);
    else {
        std::cout << "Unknown command or invalid arguments.\n";
        std::cout << "Available commands: init, add <filename>, commit -m \"<message>\", log, branch <branch-name>, checkout <ref>, merge <branch-name>\n";
        return 1;
    }

    return 0;
}
