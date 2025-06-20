#include <iostream>  
#include <fstream>     // to input/output file
#include <filesystem>  // For handling file paths
#include <string>      // To use text (strings)
#include <vector>      // For using std::vector
#include <unordered_set>  // A set of items with no duplicates,for staging
#include <unordered_map>   // A map from key to value
#include <ctime>           // To get the current date and time
#include <iomanip>          // To format output like "01", "0a" etc
#include <sstream>          // To build text strings
#include <openssl/sha.h>    // To create a hash (a unique code for content)

namespace fs = std::filesystem;//A shortcut so we can type 'fs' instead of 'std::filesystem'

class MiniGit {
private:
    fs::path rootDir;       // The current working folder
    fs::path gitDir;        // The .minigit folder
    fs::path objectsDir;    // Stores files, commits, trees
    fs::path refsDir;       // Stores branch names and their latest commits
    fs::path headPath;      // Points to the current branch
    std::unordered_set<std::string> stagingArea;      // Files you've added but not yet committed
    std::string currentBranch;    // The branch you're working on

    // Helper function to compute SHA1 hash
    std::string computeSHA1(const std::string& content) {
        unsigned char hash[SHA_DIGEST_LENGTH]; // Buffer for hash result
        SHA1(reinterpret_cast<const unsigned char*>(content.c_str()), content.size(), hash); // Convert binary hash to hexadecimal string

        std::stringstream ss;
        for (unsigned char i : hash) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(i);
        }
        return ss.str();
    }

    // Helper function to read file content
    std::string readFile(const fs::path& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Could not open file: " + filePath.string());
        }   // Read entire file content
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    // Helper function to write file content
    void writeFile(const fs::path& filePath, const std::string& content) {   // Create parent directories if they don't exist
        fs::create_directories(filePath.parent_path());
        std::ofstream file(filePath, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Could not write to file: " + filePath.string());
        }
        file.write(content.c_str(), content.size());
    }

    // Create a blob object (file content)
    std::string createBlob(const fs::path& filePath) {
        std::string content = readFile(filePath);
        std::string hash = computeSHA1(content);
        // Store in objects dir with first 2 chars as subdir
        fs::path blobPath = objectsDir / hash.substr(0, 2) / hash.substr(2);
        writeFile(blobPath, content);
        return hash;
    }

    // Create a tree object (directory structure)
    std::string createTree(const std::unordered_map<std::string, std::string>& files) {
        std::stringstream treeContent;
         // Format: "blob <hash> <filename>" for each file
        for (const auto& [path, hash] : files) {
            treeContent << "blob " << hash << " " << path << "\n";
        }
        std::string content = treeContent.str();
        std::string hash = computeSHA1(content);
        fs::path treePath = objectsDir / hash.substr(0, 2) / hash.substr(2);
        writeFile(treePath, content);
        return hash;
    }

    // Create a commit object
    std::string createCommit(const std::string& treeHash, const std::string& parentHash, const std::string& message) {
        std::time_t now = std::time(nullptr);
        std::string timestamp = std::asctime(std::localtime(&now));
        timestamp.pop_back(); // Remove newline

        std::stringstream commitContent;
        commitContent << "tree " << treeHash << "\n";
        if (!parentHash.empty()) {
            commitContent << "parent " << parentHash << "\n";
        }
        commitContent << "author MiniGit User <user@example.com>\n";
        commitContent << "date " << timestamp << "\n";
        commitContent << "\n" << message << "\n";

        std::string content = commitContent.str();
        std::string hash = computeSHA1(content);
        fs::path commitPath = objectsDir / hash.substr(0, 2) / hash.substr(2);
        writeFile(commitPath, content);
        return hash;
    }

    // Update HEAD reference
    void updateHead(const std::string& commitHash) {
        fs::path branchPath = refsDir / "heads" / currentBranch;
        writeFile(branchPath, commitHash);
        writeFile(headPath, "ref: refs/heads/" + currentBranch);
    }

    // Get current commit hash
    std::string getCurrentCommit() {
        if (fs::exists(headPath)) {
            std::string headContent = readFile(headPath);
            if (headContent.find("ref:") == 0) {
                fs::path branchPath = gitDir / headContent.substr(5);
                if (fs::exists(branchPath)) {
                    return readFile(branchPath);
                }
            } else {
                return headContent;
            }
        }
        return "";
    }

public:
// Constructor - initializes repository
    MiniGit() {
        rootDir = fs::current_path();
        gitDir = rootDir / ".minigit";
        objectsDir = gitDir / "objects";
        refsDir = gitDir / "refs";
        headPath = gitDir / "HEAD";
        currentBranch = "master";

        // Initialize repository if not exists
        if (!fs::exists(gitDir)) {
            fs::create_directory(gitDir);
            fs::create_directories(objectsDir);
            fs::create_directories(refsDir / "heads");
            updateHead(""); // Initialize HEAD
        }
    }

    // Add file to staging area
    void add(const std::string& filename) {
        fs::path filePath = rootDir / filename;
        if (!fs::exists(filePath)) {
            std::cerr << "Error: File not found - " << filename << std::endl;
            return;
        }
        stagingArea.insert(filename);
        std::cout << "Added " << filename << " to staging area" << std::endl;
    }

    // Create a new commit
    void commit(const std::string& message) {
        if (stagingArea.empty()) {
            std::cerr << "Nothing to commit (use 'add' to stage files)" << std::endl;
            return;
        }

        // Create blobs for all staged files
        std::unordered_map<std::string, std::string> files;
        for (const auto& filename : stagingArea) {
            fs::path filePath = rootDir / filename;
            std::string blobHash = createBlob(filePath);
            files[filename] = blobHash;
        }

        // Create tree and commit
        std::string treeHash = createTree(files);
        std::string parentHash = getCurrentCommit();
        std::string commitHash = createCommit(treeHash, parentHash, message);

        // Update HEAD
        updateHead(commitHash);
        stagingArea.clear();

        std::cout << "[" << currentBranch << " " << commitHash.substr(0, 7) << "] " << message << std::endl;
    }

    // Create a new branch
    void branch(const std::string& branchName) {
        fs::path branchPath = refsDir / "heads" / branchName;
        if (fs::exists(branchPath)) {
            std::cerr << "Error: Branch " << branchName << " already exists" << std::endl;
            return;
        }

        std::string currentCommit = getCurrentCommit();
        writeFile(branchPath, currentCommit);
        std::cout << "Created branch " << branchName << std::endl;
    }

    // Switch to another branch
    void checkout(const std::string& branchName) {
        fs::path branchPath = refsDir / "heads" / branchName;
        if (!fs::exists(branchPath)) {
            std::cerr << "Error: Branch " << branchName << " does not exist" << std::endl;
            return;
        }

        currentBranch = branchName;
        writeFile(headPath, "ref: refs/heads/" + branchName);
        std::cout << "Switched to branch " << branchName << std::endl;
    }

    // Show commit history
    void log() {
        std::string commitHash = getCurrentCommit();
        if (commitHash.empty()) {
            std::cout << "No commits yet" << std::endl;
            return;
        }

        while (!commitHash.empty()) {
            fs::path commitPath = objectsDir / commitHash.substr(0, 2) / commitHash.substr(2);
            if (!fs::exists(commitPath)) {
                break;
            }

            std::string content = readFile(commitPath);
            size_t messagePos = content.find("\n\n");
            std::string header = content.substr(0, messagePos);
            std::string message = content.substr(messagePos + 2);

            std::cout << "commit " << commitHash << "\n";
            std::cout << header << "\n";
            std::cout << "    " << message << "\n\n";

            // Find parent commit
            size_t parentPos = header.find("parent ");
            if (parentPos != std::string::npos) {
                size_t endPos = header.find("\n", parentPos);
                commitHash = header.substr(parentPos + 7, endPos - (parentPos + 7));
            } else {
                commitHash = "";
            }
        }
    }

    // Merge current branch with another branch
    // Shows commit history (like git log)
    void merge(const std::string& branchName) {
        fs::path otherBranchPath = refsDir / "heads" / branchName;
        if (!fs::exists(otherBranchPath)) {
            std::cerr << "Error: Branch " << branchName << " does not exist" << std::endl;
            return;
        }

        std::string otherCommit = readFile(otherBranchPath);
        std::string currentCommit = getCurrentCommit();

        // Simple merge - just create a new commit with both parents
        std::unordered_map<std::string, std::string> files;
        // In a real implementation, we would need to handle file conflicts here
        // For simplicity, we'll just keep all files from both branches
        
        std::string treeHash = createTree(files);
        std::string commitHash = createCommit(treeHash, currentCommit + " " + otherCommit, 
                                            "Merge branch '" + branchName + "' into " + currentBranch);

        updateHead(commitHash);
        std::cout << "Merged branch " << branchName << " into " << currentBranch << std::endl;
    }
};

// Main Function
int main() {
    MiniGit git;

    // Simple command-line interface
    std::string command;
    while (true) {
        std::cout << "minigit> ";
        std::getline(std::cin, command);

        if (command == "exit" || command == "quit") {
            break;
        } else if (command.substr(0, 3) == "add") {
            if (command.length() > 4) {
                git.add(command.substr(4));
            } else {
                std::cerr << "Usage: add <filename>" << std::endl;
            }
        } else if (command.substr(0, 6) == "commit") {
            if (command.length() > 8 && command.substr(6, 2) == " -m") {
                git.commit(command.substr(9));
            } else {
                std::cerr << "Usage: commit -m \"message\"" << std::endl;
            }
        } else if (command.substr(0, 6) == "branch") {
            if (command.length() > 7) {
                git.branch(command.substr(7));
            } else {
                std::cerr << "Usage: branch <branchname>" << std::endl;
            }
        } else if (command.substr(0, 7) == "checkout") {
            if (command.length() > 9) {
                git.checkout(command.substr(9));
            } else {
                std::cerr << "Usage: checkout <branchname>" << std::endl;
            }
        } else if (command == "log") {
            git.log();
        } else if (command.substr(0, 5) == "merge") {
            if (command.length() > 6) {
                git.merge(command.substr(6));
            } else {
                std::cerr << "Usage: merge <branchname>" << std::endl;
            }
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
        }
    }

    return 0;
}