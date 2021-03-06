
#include <iostream>
#include <sstream>
#include <thread>

#include <cstring>
#include <unistd.h>

#include "Unpacker.hpp"
#include "hribf_buffers.h"
#include "poll2_socket.h"
#include "CTerminal.h"

#define SCAN_VERSION "1.2.00"
#define SCAN_DATE "Oct. 1st, 2015"

std::string prefix, extension;

int max_spill_size = 0;
int file_format = -1;
unsigned long num_spills_recvd;

bool is_verbose;
bool debug_mode;
bool dry_run_mode;
bool force_overwrite;
bool shm_mode;

bool kill_all = false;
bool scan_running = false;
bool run_ctrl_exit = false;

Server poll_server;

std::ifstream input_file;

PLD_header pldHead;
PLD_data pldData;
DIR_buffer dirbuff;
HEAD_buffer headbuff;
DATA_buffer databuff;
EOF_buffer eofbuff;

Terminal *term_;

#ifndef PROG_NAME
#define PROG_NAME "ScanMain"
#endif

std::string sys_message_head = std::string(PROG_NAME) + ": ";

void start_run_control(Unpacker *core_){
	if(debug_mode){
		pldHead.SetDebugMode();
		pldData.SetDebugMode();
		dirbuff.SetDebugMode();
		headbuff.SetDebugMode();
		databuff.SetDebugMode();
		eofbuff.SetDebugMode();
	}

	// Now we're ready to read the first data buffer
	if(shm_mode){
		std::cout << std::endl;
		unsigned int data[250000];
		unsigned int shm_data[10002]; // Array to store the temporary shm data (~40 kB)
		int dummy;
		int previous_chunk;
		int current_chunk;
		int total_chunks;
		int nBytes;
		unsigned int nTotalBytes;
	
		while(true){
			previous_chunk = 0;
			current_chunk = 0;
			total_chunks = -1;
			nTotalBytes = 0;
		
			while(current_chunk != total_chunks){
				if(kill_all == true){ 
					run_ctrl_exit = true;
					return;
				}

				std::stringstream status;
				if(!poll_server.Select(dummy)){
					status << "\033[0;33m" << "[IDLE]" << "\033[0m" << " Waiting for a spill...";
					term_->SetStatus(status.str());
					continue; 
				}

				nBytes = poll_server.RecvMessage((char*)shm_data, 40008); // Read from the socket
				if(strcmp((char*)shm_data, "$CLOSE_FILE") == 0 || strcmp((char*)shm_data, "$OPEN_FILE") == 0 || strcmp((char*)shm_data, "$KILL_SOCKET") == 0){ continue; } // Poll2 network flags
				// Did not read enough bytes
				else if(nBytes < 8){
					continue;
				}
				status << "\033[0;32m" << "[RECV] " << "\033[0m" << nBytes;
				term_->SetStatus(status.str());

				if(debug_mode){ std::cout << "debug: Received " << nBytes << " bytes from the network\n"; }
				memcpy((char *)&current_chunk, &shm_data[0], 4);
				memcpy((char *)&total_chunks, &shm_data[4], 4);

				if(previous_chunk == -1 && current_chunk != 1){ // Started reading in the middle of a spill, ignore the rest of it
					if(debug_mode){ std::cout << "debug: Skipping chunk " << current_chunk << " of " << total_chunks << std::endl; }
					continue;
				}
				else if(previous_chunk != current_chunk - 1){ // We missed a spill chunk somewhere
					if(debug_mode){ std::cout << "debug: Found chunk " << current_chunk << " but expected chunk " << previous_chunk+1 << std::endl; }
					break;
				}

				previous_chunk = current_chunk;
		
				// Copy the shm spill chunk into the data array
				if(nTotalBytes + 2 + nBytes <= 1000000){ // This spill chunk will fit into the data buffer
					memcpy(&data[nTotalBytes], &shm_data[8], nBytes - 8);
					nTotalBytes += (nBytes - 8);				
				}
				else{ 
					if(debug_mode){ std::cout << "debug: Abnormally full spill buffer with " << nTotalBytes + 2 + nBytes << " bytes!\n"; }
					break; 
				}
			}
		
			if(debug_mode){ std::cout << "debug: Retrieved spill of " << nTotalBytes << " bytes (" << nTotalBytes/4 << " words)\n"; }
			if(!dry_run_mode){ 
				int word1 = 2, word2 = 9999;
				memcpy(&data[nTotalBytes], (char *)&word1, 4);
				memcpy(&data[nTotalBytes+4], (char *)&word2, 4);
				core_->ReadSpill(data, nTotalBytes/4 + 2, is_verbose); 
			}
			num_spills_recvd++;
		}
	}
	else if(file_format == 0){
		unsigned int *data = NULL;
		bool full_spill;
		bool bad_spill;
		unsigned int nBytes;
		
		if(!dry_run_mode){ data = new unsigned int[250000]; }
		
		while(databuff.Read(&input_file, (char*)data, nBytes, 1000000, full_spill, bad_spill, dry_run_mode)){ 
			if(full_spill){ 
				if(debug_mode){ 
					std::cout << "debug: Retrieved spill of " << nBytes << " bytes (" << nBytes/4 << " words)\n"; 
					std::cout << "debug: Read up to word number " << input_file.tellg()/4 << " in input file\n";
				}
				if(!dry_run_mode){ 
					if(!bad_spill){ 
						core_->ReadSpill(data, nBytes/4, is_verbose); 
					}
					else{ std::cout << " WARNING: Spill has been flagged as corrupt, skipping (at word " << input_file.tellg()/4 << " in file)!\n"; }
				}
			}
			else if(debug_mode){ 
				std::cout << "debug: Retrieved spill fragment of " << nBytes << " bytes (" << nBytes/4 << " words)\n"; 
				std::cout << "debug: Read up to word number " << input_file.tellg()/4 << " in input file\n";
			}
			num_spills_recvd++;
		}

		if(eofbuff.Read(&input_file) && eofbuff.Read(&input_file)){
			std::cout << sys_message_head << "Encountered double EOF buffer.\n";
		}
		else{
			std::cout << sys_message_head << "Failed to find end of file buffer!\n";
		}
		
		if(!dry_run_mode){ delete[] data; }
	}
	else if(file_format == 1){
		unsigned int *data = NULL;
		int nBytes;
		
		if(!dry_run_mode){ data = new unsigned int[max_spill_size+2]; }
		
		while(pldData.Read(&input_file, (char*)data, nBytes, 4*max_spill_size, dry_run_mode)){ 
			if(debug_mode){ 
				std::cout << "debug: Retrieved spill of " << nBytes << " bytes (" << nBytes/4 << " words)\n"; 
				std::cout << "debug: Read up to word number " << input_file.tellg()/4 << " in input file\n";
			}
			
			if(!dry_run_mode){ 
				int word1 = 2, word2 = 9999;
				memcpy(&data[(nBytes/4)], (char *)&word1, 4);
				memcpy(&data[(nBytes/4)+1], (char *)&word2, 4);
				core_->ReadSpill(data, nBytes/4 + 2, is_verbose); 
			}
			num_spills_recvd++;
		}

		if(eofbuff.ReadHeader(&input_file)){
			std::cout << sys_message_head << "Encountered EOF buffer.\n";
		}
		else{
			std::cout << sys_message_head << "Failed to find end of file buffer!\n";
		}
		
		if(!dry_run_mode){ delete[] data; }
	}
	else if(file_format == 2){
	}
	
	run_ctrl_exit = true;
}

void start_cmd_control(){
	std::string cmd = "", arg;

	bool cmd_ready = true;
	
	while(true){
		cmd = term_->GetCommand();
		if(cmd == "CTRL_D"){ cmd = "quit"; }
		else if(cmd == "CTRL_C"){ continue; }		
		term_->flush();

		if(cmd_ready){
			if(cmd == "quit" || cmd == "exit"){
				if(scan_running){ std::cout << sys_message_head << "Warning! Cannot quit while scan is running\n"; }
				else{
					kill_all = true;
					while(!run_ctrl_exit){ sleep(1); }
					break;
				}
			}
			else{ std::cout << sys_message_head << "Unknown command '" << cmd << "'\n"; }
		}
	}		
}

std::string GetExtension(std::string filename_, std::string &prefix){
	size_t count = 0;
	size_t last_index = 0;
	std::string output = "";
	prefix = "";
	
	// Find the final period in the filename
	for(count = 0; count < filename_.size(); count++){
		if(filename_[count] == '.'){ last_index = count; }
	}
	
	// Get the filename prefix and the extension
	for(size_t i = 0; i < count; i++){
		if(i < last_index){ prefix += filename_[i]; }
		else if(i > last_index){ output += filename_[i]; }
	}
	
	return output;
}

void help(char *name_, Unpacker *core_){
	core_->SyntaxStr(name_, " ");
	std::cout << "  Available options:\n";
	std::cout << "   --help     - Display this dialogue\n";
	std::cout << "   --version  - Display version information\n";
	std::cout << "   --debug    - Enable readout debug mode\n";
	std::cout << "   --shm      - Enable shared memory readout\n";
	std::cout << "   --ldf      - Force use of ldf readout\n";
	std::cout << "   --pld      - Force use of pld readout\n";
	std::cout << "   --root     - Force use of root readout\n";
	std::cout << "   --quiet    - Toggle off verbosity flag\n";
	std::cout << "   --dry-run  - Extract spills from file, but do no processing\n";
	std::cout << "   --fast-fwd [word] - Skip ahead to a specified word in the file (start of file at zero)\n";
	core_->Help("   ");
}

int main(int argc, char *argv[]){
	if(argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)){ // A stupid way to do this... for now.
		Unpacker *temp_core = GetCore();
		help(argv[0], temp_core);
		delete temp_core;
		return 0;
	}
	else if(argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)){ // Display version information
		std::cout << " " << PROG_NAME << "-------v" << SCAN_VERSION << " (" << SCAN_DATE << ")\n";
		std::cout << " |hribf_buffers-v" << HRIBF_BUFFERS_VERSION << " (" << HRIBF_BUFFERS_DATE << ")\n";
		std::cout << " |CTerminal-----v" << CTERMINAL_VERSION << " (" << CTERMINAL_DATE << ")\n";
		std::cout << " |poll2_socket--v" << POLL2_SOCKET_VERSION << " (" << POLL2_SOCKET_DATE << ")\n";
		return 0;
	}
	
	debug_mode = false;
	dry_run_mode = false;
	shm_mode = false;

	num_spills_recvd = 0;

	long file_start_offset = 0;

	// Fill the argument list.
	std::deque<std::string> scan_args;
	std::deque<std::string> core_args;
	int arg_index = 1;
	while(arg_index < argc){
		scan_args.push_back(std::string(argv[arg_index++]));
	}

	Unpacker *core = GetCore(); // Get a pointer to the main Unpacker object.
	
	// Loop through the arg list and extract ScanMain arguments.
	std::string current_arg;
	while(!scan_args.empty()){
		current_arg = scan_args.front();
		scan_args.pop_front();
	
		if(current_arg == "--debug"){ 
			core->SetDebugMode();
			debug_mode = true;
		}
		else if(current_arg == "--dry-run"){
			dry_run_mode = true;
		}
		else if(current_arg == "--fast-fwd"){
			if(scan_args.empty()){
				std::cout << " Error: Missing required argument to option '--fast-fwd'!\n";
				help(argv[0], core);
				return 1;
			}
			file_start_offset = atoll(scan_args.front().c_str());
			scan_args.pop_front();
		}
		else if(current_arg == "--quiet"){
			is_verbose = false;
		}
		else if(current_arg == "--shm"){ 
			file_format = 0;
			shm_mode = true;
			std::cout << " Using shm mode!\n";
		}
		else if(current_arg == "--ldf"){ 
			file_format = 0;
		}
		else if(current_arg == "--pld"){ 
			file_format = 1;
		}
		else if(current_arg == "--root"){ 
			file_format = 2;
		}
		else{ core_args.push_back(current_arg); } // Unrecognized option.
	}

	std::string input_filename = "";
	if(!core->SetArgs(core_args, input_filename)){ 
		help(argv[0], core);
		return 1; 
	}

	if(!shm_mode){
		extension = GetExtension(input_filename, prefix);
		if(file_format != -1){
			if(file_format == 0){ std::cout << sys_message_head << "Forcing ldf file readout.\n"; }
			else if(file_format == 1){ std::cout << sys_message_head << "Forcing pld file readout.\n"; }
			else if(file_format == 2){ std::cout << sys_message_head << "Forcing root file readout.\n"; }
		}
		else{
			if(prefix == ""){
				std::cout << " ERROR: Input filename was not specified!\n";
				return 1;
			}
		
			if(extension == "ldf"){ // List data format file
				file_format = 0;
			}
			else if(extension == "pld"){ // Pixie list data file format
				file_format = 1;
			}
			else if(extension == "root"){ // Pixie list data file format
				file_format = 2;
			}
			else{
				std::cout << " ERROR: Invalid file format '" << extension << "'\n";
				std::cout << "  The current valid data formats are:\n";
				std::cout << "   ldf - list data format (HRIBF)\n";
				std::cout << "   pld - pixie list data format\n";
				std::cout << "   root - root file containing raw pixie data\n";
				return 1;
			}
		}
	}

	// Initialize the Unpacker object.
	core->Initialize(sys_message_head);

	// Initialize the command terminal
	Terminal terminal;
	term_ = &terminal;

	if(!shm_mode){
		std::cout << sys_message_head << "Using file prefix " << prefix << ".\n";
		input_file.open((prefix+"."+extension).c_str(), std::ios::binary);
		if(!input_file.is_open() || !input_file.good()){
			std::cout << " ERROR: Failed to open input file '" << prefix+"."+extension << "'! Check that the path is correct.\n";
			input_file.close();
			return 1;
		}
	}
	else{
		if(!poll_server.Init(5555, 1)){
			std::cout << " ERROR: Failed to open shm socket 5555!\n";
			return 1;
		}

		std::string temp_name = std::string(PROG_NAME);
		
		// Only initialize the terminal if this is shared-memory mode
		terminal.Initialize(("."+temp_name+".cmd").c_str());
		terminal.SetPrompt((temp_name+" $ ").c_str());
		terminal.AddStatusWindow();

		std::cout << "\n " << PROG_NAME << " v" << SCAN_VERSION << "\n"; 
		std::cout << " ==  ==  ==  ==  == \n\n"; 
	}

	if(debug_mode){ std::cout << sys_message_head << "Using debug mode.\n"; }
	if(dry_run_mode){ std::cout << sys_message_head << "Doing a dry run.\n"; }
	if(shm_mode){ 
		std::cout << sys_message_head << "Using shared-memory mode.\n"; 
		std::cout << sys_message_head << "Listening on poll2 SHM port 5555\n";
	}

	if(!shm_mode){
		// Start reading the file
		// Every poll2 ldf file starts with a DIR buffer followed by a HEAD buffer
		int num_buffers;
		if(file_format == 0){
			dirbuff.Read(&input_file, num_buffers);
			headbuff.Read(&input_file);
			
			// Let's read out the file information from these buffers
			std::cout << "\n 'DIR ' buffer-\n";
			std::cout << "  Run number: " << dirbuff.GetRunNumber() << std::endl;
			std::cout << "  Number buffers: " << num_buffers << std::endl << std::endl;
	
			std::cout << " 'HEAD' buffer-\n";
			std::cout << "  Facility: " << headbuff.GetFacility() << std::endl;
			std::cout << "  Format: " << headbuff.GetFormat() << std::endl;
			std::cout << "  Type: " << headbuff.GetType() << std::endl;
			std::cout << "  Date: " << headbuff.GetDate() << std::endl;
			std::cout << "  Title: " << headbuff.GetRunTitle() << std::endl;
			std::cout << "  Run number: " << headbuff.GetRunNumber() << std::endl << std::endl;
		}
		else if(file_format == 1){
			pldHead.Read(&input_file);
			
			max_spill_size = pldHead.GetMaxSpillSize();
			
			// Let's read out the file information from these buffers
			std::cout << "\n 'HEAD' buffer-\n";
			std::cout << "  Facility: " << pldHead.GetFacility() << std::endl;
			std::cout << "  Format: " << pldHead.GetFormat() << std::endl;
			std::cout << "  Start: " << pldHead.GetStartDate() << std::endl;
			std::cout << "  Stop: " << pldHead.GetEndDate() << std::endl; 
			std::cout << "  Title: " << pldHead.GetRunTitle() << std::endl;
			std::cout << "  Run number: " << pldHead.GetRunNumber() << std::endl;
			std::cout << "  Max spill: " << pldHead.GetMaxSpillSize() << " words\n";
			std::cout << "  ACQ Time: " << pldHead.GetRunTime() << " seconds\n\n";
		}
		else if(file_format == 2){
		}
		
		// Fast forward in the file
		if(file_start_offset != 0){
			std::cout << " Skipping ahead to word no. " << file_start_offset << " in file\n";
			input_file.seekg(file_start_offset*4);
			std::cout << " Input file is now at " << input_file.tellg() << " bytes\n";
		}

		start_run_control(core);
	}
	else{ 
		// Start the run control thread
		std::cout << "\nStarting data control thread\n";
		std::thread runctrl(start_run_control, core);
	
		// Start the command control thread. This needs to be the last thing we do to
		// initialize, so the user cannot enter commands before setup is complete
		std::cout << "Starting command thread\n\n";
		std::thread comctrl(start_cmd_control);

		// Synchronize the threads and wait for completion
		comctrl.join();
		runctrl.join();
	
		// Close the socket and restore the terminal
		terminal.Close();
		poll_server.Close();
		
		//Reprint the leader as the carriage was returned
		std::cout << "Running " << PROG_NAME << " v" << SCAN_VERSION << " (" << SCAN_DATE << ")\n";
	}
	
	std::cout << sys_message_head << "Retrieved " << num_spills_recvd << " spills!\n";

	input_file.close();	

	// Clean up detector driver
	std::cout << "\nCleaning up...\n";
	
	core->PrintStatus(sys_message_head);
	core->Close();
	delete core;
	
	return 0;
}
