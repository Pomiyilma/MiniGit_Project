#include <iostream> 
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

void init() {
    if (!fs::exists(".minigit")) {
        fs::create_directory(".minigit");
        fs::create_directory(".minigit/objects");
        fs::create_directory(".minigit/commits");
        fs::create_directory(".minigit/refs");
        fs::create_directory(".minigit/refs/heads");

        std::ofstream head_file(fs::path(".minigit/HEAD"));
        if(head_file.is_open()){
            head_file << "ref: refs/heads/master";
            head_file.close();
        }else{
            std::cerr<<"Error: Could not open .minigit/HEAD for writing.\n";
            return;
        }

        std::cout << "Initialized empty MiniGit repository in .minigit/\n";
    } else {
        std::cout << "MiniGit is already initialized.\n";
    }
}

void checkout(const std::string& target) {
    std::ifstream branchIn(".minigit/refs/heads/" + target);
    std::string commitID;

    if (branchIn) {
        std::getline(branchIn, commitID);
        branchIn.close();

        std::ofstream headOut(".minigit/HEAD");
        if (headOut) {
            headOut << "ref: refs/heads/" << target;
            headOut.close();
        }
    } else {
        commitID = target;
        std::ofstream headOut(".minigit/HEAD");
        if (headOut) {
            headOut << commitID;
            headOut.close();
        }
    }

    std::ifstream commitIn(".minigit/commits/" + commitID);
    if (!commitIn) {
        std::cerr << "Error: Could not locate commit: " << commitID << "\n";
        return;
    }

    std::string entry;
    while (std::getline(commitIn, entry)) {
        auto splitPos = entry.find(' ');
        if (splitPos == std::string::npos) continue;

        std::string fileName = entry.substr(0, splitPos);
        std::string blobID = entry.substr(splitPos + 1);

        std::ifstream blobIn(".minigit/objects/" + blobID);
        if (!blobIn) {
            std::cerr << "Warning: Missing blob object " << blobID << "\n";
            continue;
        }

        std::ofstream fileOut(fileName);
        std::string lineData;
        while (std::getline(blobIn, lineData)) {
            fileOut << lineData << '\n';
        }

        blobIn.close();
        fileOut.close();
        std::cout << "Restored file: " << fileName << "\n";
    }

    commitIn.close();
    std::cout << "Checkout done for: " << target << "\n";
}


int main() {
    std::string command;

    while (true) {
        std::cout << "Enter command (init, checkout <branch/hash>, exit): ";
        std::cin >> command;

        if (command == "init") {
            init();
        } else if (command == "checkout") {
            std::string target;
            std::cin >> target;
            checkout(target);
        } else if (command == "exit") {
            std::cout << "Exiting MiniGit.\n";
            break;
        } else {
            std::cout << "Unknown command.\n";
        }
    }
    return 0;
} 