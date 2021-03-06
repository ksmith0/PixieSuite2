#ifndef UNPACKER_HPP
#define UNPACKER_HPP

#include <deque>
#include <vector>
#include <string>

class ChannelEvent;

class TFile;
class TTree;

class Unpacker{
  protected:
	static const unsigned int TOTALREAD = 1000000; /// Maximum number of data words to read.
	static const unsigned int maxWords = 131072; /// Maximum number of data words for revision D.
	
	unsigned int event_width; /// The width of the raw event in pixie clock ticks (8 ns).
	
	bool debug_mode; /// True if debug mode is set.
	bool init; /// True if the class has been properly initialized.

	std::deque<ChannelEvent*> eventList; /// The list of all events in the spill.
	std::deque<ChannelEvent*> rawEvent; /// The list of all events in the event window.

	TFile *root_file;
	TTree *root_tree;

	/** Clear all events in the raw event. WARNING! This method will delete all events in the
	 * event list. This could cause seg faults if the events are used elsewhere.
	 */	
	void ClearRawEvent();

	/** Clear all events in the spill event list. WARNING! This method will delete all events in the
	 * event list. This could cause seg faults if the events are used elsewhere.
	 */	
	void ClearEventList();
	
	/** Delete an event off the front of the event list. WARNING! This method will delete all events
	 * in the event list. This could cause seg faults if the events are used elsewhere.
	 */	
	void DeleteCurrentEvent();

	/** Process all events in the event list. This method will do nothing
	 *  unless it is overloaded by a derived class.
	 */
	virtual void ProcessRawEvent();
	
	/** Scan the time sorted event list and package the events into a raw
	 * event with a size governed by the event width.
	 */
	void ScanList();
	
	/// Scan the event list and sort it by timestamp.
	void SortList();
	
	/** Called form ReadSpill. Scan the current spill and construct a list of
	 * events which fired by obtaining the module, channel, trace, etc. of the
	 * timestamped event. This method will construct the event list for
	 * later processing.
	 */	
	int ReadBuffer(unsigned int *buf, unsigned long &bufLen);
	
  public:
  	/// Default constructor.
	Unpacker();
	
	/// Destructor.
	~Unpacker();

	/** Initialize the Unpacker object. Does nothing useful if not overloaded
	 * by a derived class.
	 */
	virtual bool Initialize(std::string prefix_="");
	
	/** Initialize the root output. Does nothing useful if not overloaded
	 * by a derived class.
	 */
	virtual bool InitRootOutput(std::string fname_, bool overwrite_=true){ return false; }

	/// Return true if Unpacker was properly initialized.
	bool IsInit(){ return init; }

	/// Toggle debug mode on / off.
	bool SetDebugMode(bool state_=true){ return (debug_mode = state_); }

	/// Set the width of events in pixie16 clock ticks.
	unsigned int SetEventWidth(unsigned int width_){ return (event_width = width_); }
	
	/** ReadSpill is responsible for constructing a list of pixie16 events from
	 * a raw data spill. This method performs sanity checks on the spill and
	 * calls ReadBuffer in order to construct the event list.
	 */	
	bool ReadSpill(unsigned int *data, unsigned int nWords, bool is_verbose=true);
	
	/// Return the syntax string for this program.
	virtual void SyntaxStr(const char *name_, std::string prefix_=""){ std::cout << prefix_ << "SYNTAX: " << std::string(name_) << " <options> <input>\n"; }

	/// Print a help dialogue.
	virtual void Help(std::string prefix_=""){}
	
	/// Scan input arguments and set class variables.
	virtual bool SetArgs(std::deque<std::string> &args_, std::string &filename_){ return true; }

	/// Print a status message.
	virtual void PrintStatus(std::string prefix_=""){}

	/// Empty the raw event and the event list.
	void Close();
};

extern Unpacker *GetCore(); /// External function which returns a pointer to a class derived from Unpacker.

#endif
