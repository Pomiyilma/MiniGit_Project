#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem> // C++17 for directory operations
#include <chrono>     // For timestamp
#include <iomanip>    // For string formatting
#include <sstream>    // For string streams
#include <map>        // For std::map used in CommitNode and staging_area_

namespace fs = std::filesystem;

// A simple placeholder for a hashing function.
// In a real Git system, you would use a robust cryptographic hash like SHA-1.
// For this simplified version, we'll create a "hash" based on content and timestamp.
std::string generate_simple_hash(const std::string& content) {
    // Generate a timestamp string
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    // Combine content length and timestamp for a simple, pseudo-unique identifier
    std::hash<std::string> hasher;
    std::string data_to_hash = content + std::to_string(timestamp) + std::to_string(hasher(content));
    size_t content_hash = hasher(data_to_hash);

    std::stringstream ss;
    ss << std::hex << std::setw(32) << std::setfill('0') << content_hash; // Use 32 hex chars for a hash-like appearance
    return ss.str();
}

// Represents a Git commit object.
// Contains metadata like message, timestamp, parent, and a map of tracked files (filename to blob hash).
class CommitNode {
public:
    std::string hash; // The unique hash of this commit object
    std::string message; // Commit message
    std::string timestamp; // When the commit was created
    std::string parent_hash; // Hash of the parent commit (empty for initial commit)
    std::vector<std::string> parent_hashes; // For merges (not fully utilized in this basic implementation)
    std::map<std::string, std::string> tracked_files; // Maps filenames to their corresponding blob hashes

    CommitNode(const std::string& msg, const std::string& parent_h, const std::map<std::string, std::string>& files_map)
        : message(msg), parent_hash(parent_h), tracked_files(files_map) {
        // Generate timestamp upon creation
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
        timestamp = ss.str();
    }

    // Serializes the CommitNode into a string format, similar to Git's commit object structure.
    std::string serialize() const {
        std::stringstream ss;
        // A real Git commit object includes a tree hash. Here, we directly list files.
        ss << "tree\n"; // Placeholder for tree hash
        for (const auto& pair : tracked_files) {
            ss << "blob " << pair.second << " " << pair.first << "\n"; // Format: type hash filename
        }
        ss << "parent " << parent_hash << "\n"; // Will be empty for the very first commit
        ss << "author Your Name <your.email@example.com> " << timestamp << "\n"; // Placeholder author/committer
        ss << "committer Your Name <your.email@example.com> " << timestamp << "\n";
        ss << "\n"; // Blank line separates header from message body
        ss << message << "\n";
        return ss.str();
    }

    // Deserializes a string content (read from a commit object file) back into a CommitNode object.
    static CommitNode deserialize(const std::string& commit_hash_from_filename, const std::string& content) {
        std::string msg = "";
        std::string parent = "";
        std::map<std::string, std::string> files;
        std::string loaded_timestamp = ""; // To store the timestamp parsed from the content

        std::istringstream iss(content);
        std::string line;
        bool in_message_block = false;

        while (std::getline(iss, line)) {
            if (line.empty() && !in_message_block) { // An empty line signifies the start of the commit message
                in_message_block = true;
                continue;
            }

            if (in_message_block) {
                msg += line + "\n"; // Append message lines to build the full message
            } else {
                // Parse header lines
                if (line.rfind("blob ", 0) == 0) { // Line describing a tracked file (blob)
                    std::istringstream blob_line_ss(line);
                    std::string type, hash_val, filename;
                    blob_line_ss >> type >> hash_val >> filename;
                    files[filename] = hash_val;
                } else if (line.rfind("parent ", 0) == 0) { // Parent commit hash
                    parent = line.substr(strlen("parent "));
                } else if (line.rfind("author ", 0) == 0) { // Author line, contains timestamp
                    // Attempt to extract the timestamp from the author line
                    // Assumes a format like "author Name <email> YYYY-MM-DD HH:MM:SS"
                    size_t date_time_start = line.rfind(" ", line.length() - 1 - 19); // Find space before YYYY-MM-DD HH:MM:SS
                    if (date_time_start != std::string::npos) {
                        loaded_timestamp = line.substr(date_time_start + 1, 19); // Extract 19 characters for the timestamp
                    }
                }
            }
        }

        // Remove any trailing newline character added during message accumulation
        if (!msg.empty() && msg.back() == '\n') {
            msg.pop_back();
        }

        // Construct the CommitNode with parsed data
        CommitNode commit(msg, parent, files);
        commit.hash = commit_hash_from_filename; // Set the hash (it's derived from the filename when read)
        commit.timestamp = loaded_timestamp.empty() ? commit.timestamp : loaded_timestamp; // Use loaded timestamp or the one generated by constructor if not found
        return commit;
    }
};

// The main MiniGit class, managing repository operations.
class MiniGit {
public:
    // Constructor initializes the base directory name for the repository.
    MiniGit() : minigit_dir_name_(".minigit") {}

    // Implements the 'minigit init' command to set up a new repository.
    void init() {
        if (fs::exists(minigit_dir_name_)) {
            std::cout << "Reinitialized existing MiniGit repository in " << minigit_dir_name_ << std::endl;
            return;
        }

        // Create core MiniGit directories
        fs::create_directory(minigit_dir_name_);
        fs::create_directory(minigit_dir_name_ + "/objects");    // For blobs and commit objects
        fs::create_directory(minigit_dir_name_ + "/refs");       // For references (branches, tags)
        fs::create_directory(minigit_dir_name_ + "/refs/heads"); // For local branches

        // Create HEAD file, pointing to the default branch (e.g., main)
        std::ofstream head_file(minigit_dir_name_ + "/HEAD");
        if (head_file.is_open()) {
            head_file << "ref: refs/heads/main" << std::endl;
            head_file.close();
        } else {
            std::cerr << "Error: Could not create HEAD file." << std::endl;
        }

        // Create initial empty branch file for 'main'. It will be empty until the first commit.
        std::ofstream main_branch_file(minigit_dir_name_ + "/refs/heads/main");
        if (main_branch_file.is_open()) {
            main_branch_file.close();
        } else {
            std::cerr << "Error: Could not create main branch file." << std::endl;
        }

        std::cout << "Initialized empty MiniGit repository in " << minigit_dir_name_ << std::endl;
    }

    // Saves content as a 'blob' object in the .minigit/objects directory and returns its hash.
    std::string save_blob(const std::string& file_content) {
        std::string hash = generate_simple_hash(file_content);
        std::string blob_path = minigit_dir_name_ + "/objects/" + hash;

        std::ofstream outfile(blob_path);
        if (outfile.is_open()) {
            outfile << file_content;
            outfile.close();
            return hash;
        } else {
            std::cerr << "Error: Could not save blob to " << blob_path << std::endl;
            return "";
        }
    }

    // Reads the content of a blob given its hash.
    std::string read_blob(const std::string& hash) {
        std::string blob_path = minigit_dir_name_ + "/objects/" + hash;
        std::ifstream infile(blob_path);
        if (infile.is_open()) {
            std::string content((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
            infile.close();
            return content;
        } else {
            std::cerr << "Error: Could not read blob from " << blob_path << std::endl;
            return "";
        }
    }

private:
    std::string minigit_dir_name_;
    std::map<std::string, std::string> staging_area_; // filename -> blob_hash for staged files

    // Helper function to determine the hash of the current HEAD commit.
    std::string get_head_commit_hash() {
        std::string head_path = minigit_dir_name_ + "/HEAD";
        std::ifstream head_file(head_path);
        std::string head_content;
        if (head_file.is_open()) {
            std::getline(head_file, head_content);
            head_file.close();
        } else {
            return ""; // HEAD file doesn't exist (e.g., before init or first commit)
        }

        // Check if HEAD points to a ref (e.g., "ref: refs/heads/main")
        if (head_content.rfind("ref: ", 0) == 0) {
            std::string ref_path = head_content.substr(5); // Extract "refs/heads/main"
            std::string full_ref_path = minigit_dir_name_ + "/" + ref_path;
            std::ifstream ref_file(full_ref_path);
            std::string commit_hash;
            if (ref_file.is_open()) {
                std::getline(ref_file, commit_hash);
                ref_file.close();
                return commit_hash; // Return the commit hash stored in the branch file
            } else {
                return ""; // Branch file not found or empty
            }
        } else {
            return head_content; // HEAD directly points to a commit hash (detached HEAD state)
        }
    }

    // Helper function to update the HEAD pointer (and the branch it points to) to a new commit hash.
    void update_head(const std::string& commit_hash) {
        std::string head_path = minigit_dir_name_ + "/HEAD";
        std::ifstream head_file_read(head_path);
        std::string head_content;
        bool head_exists = head_file_read.is_open() && std::getline(head_file_read, head_content);
        if (head_exists) {
            head_file_read.close();
        } else {
            std::cerr << "Error: HEAD file not found during update." << std::endl;
            return;
        }

        // If HEAD points to a reference, update that reference file
        if (head_content.rfind("ref: ", 0) == 0) {
            std::string ref_path = head_content.substr(5);
            std::string full_ref_path = minigit_dir_name_ + "/" + ref_path;
            std::ofstream ref_file(full_ref_path);
            if (ref_file.is_open()) {
                ref_file << commit_hash << std::endl;
                ref_file.close();
            } else {
                std::cerr << "Error: Could not update branch ref file: " << full_ref_path << std::endl;
            }
        } else {
            // If HEAD is detached (pointing directly to a commit), update HEAD itself
            std::ofstream head_file_write(head_path);
            if (head_file_write.is_open()) {
                head_file_write << commit_hash << std::endl;
                head_file_write.close();
            } else {
                std::cerr << "Error: Could not update HEAD file." << std::endl;
            }
        }
    }

    // Helper function to save a CommitNode object to the objects directory.
    void save_commit_object(const CommitNode& commit) {
        std::string commit_path = minigit_dir_name_ + "/objects/" + commit.hash;
        std::ofstream outfile(commit_path);
        if (outfile.is_open()) {
            outfile << commit.serialize();
            outfile.close();
        } else {
            std::cerr << "Error: Could not save commit object to " << commit_path << std::endl;
        }
    }

    // Helper function to read a CommitNode object from the objects directory given its hash.
    CommitNode read_commit_object(const std::string& commit_hash) {
        std::string commit_path = minigit_dir_name_ + "/objects/" + commit_hash;
        std::ifstream infile(commit_path);
        std::string content((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
        infile.close();

        if (content.empty()) {
            return CommitNode("", "", {}); // Return an invalid/empty commit if file not found or empty
        }

        return CommitNode::deserialize(commit_hash, content);
    }

public: // Public methods that map to CLI commands
    // Implements the 'minigit add <filename>' command.
    void add(const std::string& filename) {
        if (!fs::exists(minigit_dir_name_)) {
            std::cerr << "Error: Not a minigit repository. Please run 'minigit init' first." << std::endl;
            return;
        }
        if (!fs::exists(filename)) {
            std::cerr << "Error: File '" << filename << "' not found." << std::endl;
            return;
        }

        std::ifstream infile(filename);
        if (infile.is_open()) {
            std::string content((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
            infile.close();
            std::string blob_hash = save_blob(content); // Save file content as a blob
            if (!blob_hash.empty()) {
                staging_area_[filename] = blob_hash; // Add to in-memory staging area
                std::cout << "Staged file: " << filename << " with blob hash: " << blob_hash << std::endl;
            }
        } else {
            std::cerr << "Error: Could not open file " << filename << std::endl;
        }
    }

    // Implements the 'minigit commit -m "<message>"' command.
    void commit(const std::string& message) {
        // Basic checks for repository existence and staged files
        if (!fs::exists(minigit_dir_name_)) {
            std::cerr << "Error: Not a minigit repository (or any of the parent directories): " << minigit_dir_name_ << std::endl;
            std::cerr << "Please run 'minigit init' first." << std::endl;
            return;
        }

        if (staging_area_.empty()) {
            std::cout << "Nothing to commit, working tree clean" << std::endl;
            return;
        }

        std::string parent_commit_hash = get_head_commit_hash(); // Get the hash of the current HEAD commit

        // Create a new CommitNode object
        CommitNode new_commit(message, parent_commit_hash, staging_area_);
        // Generate a hash for the new commit based on its serialized content
        new_commit.hash = generate_simple_hash(new_commit.serialize());

        save_commit_object(new_commit);      // Save the commit object to disk
        update_head(new_commit.hash);        // Update HEAD to point to the new commit
        staging_area_.clear();               // Clear the staging area after committing

        std::cout << "Committed successfully: " << new_commit.hash.substr(0, 7) << " " << message << std::endl;
    }

    // Implements the 'minigit log' command to display commit history.
    void log() {
        if (!fs::exists(minigit_dir_name_)) {
            std::cerr << "Error: Not a minigit repository (or any of the parent directories): " << minigit_dir_name_ << std::endl;
            return;
        }

        std::cout << "Commit History:" << std::endl;
        std::string current_commit_hash = get_head_commit_hash(); // Start from the current HEAD commit

        if (current_commit_hash.empty()) {
            std::cout << "No commits yet." << std::endl;
            return;
        }

        // Traverse the commit history backwards using parent_hash
        while (!current_commit_hash.empty()) {
            CommitNode commit = read_commit_object(current_commit_hash);
            if (commit.hash.empty()) {
                std::cerr << "Error: Could not read commit object for hash: " << current_commit_hash << std::endl;
                break; // Stop if a commit object cannot be read
            }

            std::cout << "commit " << commit.hash << std::endl;
            std::cout << "Date:   " << commit.timestamp << std::endl;
            std::cout << "\n    " << commit.message << std::endl;
            std::cout << std::endl;

            current_commit_hash = commit.parent_hash; // Move to the parent commit
        }
    }

    // Implements the 'minigit branch <branch-name>' command.
    void branch(const std::string& branch_name) {
        if (!fs::exists(minigit_dir_name_)) {
            std::cerr << "Error: Not a minigit repository. Please run 'minigit init' first." << std::endl;
            return;
        }

        // Optional: Add more robust validation for branch_name (e.g., no spaces, slashes)
        if (branch_name.empty()) {
            std::cerr << "Error: Branch name cannot be empty." << std::endl;
            return;
        }

        std::string current_commit_hash = get_head_commit_hash();

        if (current_commit_hash.empty()) {
            std::cerr << "Error: Cannot create a branch when no commits exist. Make an initial commit first." << std::endl;
            return;
        }

        std::string branch_file_path = minigit_dir_name_ + "/refs/heads/" + branch_name;

        // In a simple MiniGit, we might allow overwriting or inform if it exists.
        // A real Git would often require -f to overwrite an existing branch.
        if (fs::exists(branch_file_path)) {
            std::cout << "Warning: Branch '" << branch_name << "' already exists. Its reference will be updated." << std::endl;
        }

        std::ofstream branch_file(branch_file_path);
        if (branch_file.is_open()) {
            branch_file << current_commit_hash << std::endl;
            branch_file.close();
            std::cout << "Branch '" << branch_name << "' created, pointing to commit " << current_commit_hash.substr(0, 7) << std::endl;
        } else {
            std::cerr << "Error: Could not create branch file for '" << branch_name << "' at " << branch_file_path << std::endl;
        }
    }


    // A test function to demonstrate the save_blob and read_blob functionalities.
    void test_blob() {
        std::cout << "--- Testing Blob Functionality ---" << std::endl;

        std::string test_content1 = "Hello, MiniGit!";
        std::string hash1 = save_blob(test_content1);
        std::cout << "Content: \"" << test_content1 << "\", Saved as hash: " << hash1 << std::endl;
        std::string read_content1 = read_blob(hash1);
        std::cout << "Read content for hash " << hash1 << ": \"" << read_content1 << "\"" << std::endl;
        std::cout << "Content matches: " << (test_content1 == read_content1 ? "true" : "false") << std::endl;

        std::cout << "\n";

        std::string test_content2 = "This is some different content for a second blob.";
        std::string hash2 = save_blob(test_content2);
        std::cout << "Content: \"" << test_content2 << "\", Saved as hash: " << hash2 << std::endl;
        std::string read_content2 = read_blob(hash2);
        std::cout << "Read content for hash " << hash2 << ": \"" << read_content2 << "\"" << std::endl;
        std::cout << "Content matches: " << (test_content2 == read_content2 ? "true" : "false") << std::endl;

        std::cout << "\n";

        std::string test_content3 = "Hello, MiniGit!"; // Same content as test_content1
        std::string hash3 = save_blob(test_content3);
        std::cout << "Content: \"" << test_content3 << "\", Saved as hash: " << hash3 << std::endl;
        std::string read_content3 = read_blob(hash3);
        std::cout << "Read content for hash " << hash3 << \": \"" << read_content3 << "\"" << std::endl;
        std::cout << "Content matches: " << (test_content3 == read_content3 ? "true" : "false") << std::endl;
        std::cout << "Hashes are the same for identical content: " << (hash1 == hash3 ? "true" : "false") << std::endl;
    }
};

// Main function to parse command-line arguments and call MiniGit commands.
int main(int argc, char* argv[]) {
    MiniGit minigit;

    if (argc < 2) {
        std::cout << "Usage: minigit <command> [arguments]" << std::endl;
        std::cout << "Available commands: init, add, commit, log, branch, test_blob" << std::endl;
        return 1;
    }

    std::string command = argv[1];

    if (command == "init") {
        minigit.init();
    } else if (command == "add") {
        if (argc < 3) {
            std::cerr << "Usage: minigit add <filename>" << std::endl;
            return 1;
        }
        std::string filename = argv[2];
        minigit.add(filename);
    } else if (command == "commit") {
        if (argc < 4 || std::string(argv[2]) != "-m") {
            std::cerr << "Usage: minigit commit -m \"<message>\"" << std::endl;
            return 1;
        }
        std::string message = argv[3];
        minigit.commit(message);
    } else if (command == "log") {
        minigit.log();
    } else if (command == "branch") { // New command handler for 'branch'
        if (argc < 3) {
            std::cerr << "Usage: minigit branch <branch-name>" << std::endl;
            return 1;
        }
        std::string branch_name = argv[2];
        minigit.branch(branch_name);
    }
    else if (command == "test_blob") {
        minigit.test_blob();
    } else {
        std::cout << "Unknown command: " << command << std::endl;
    }

    return 0;
}