#pragma once

namespace RP
{
	// A command channel for a running game, so a test does not cost a build.
	//
	// Every verification tonight cost a build, a deploy, a relaunch, and the owner
	// walking somewhere in Diamond City -- and after three hours the stranded
	// requeue was STILL unproven, because proving it meant finding one named NPC
	// on foot. This is the answer to that: something outside the game asks a
	// question and gets an answer in under a second.
	//
	// SHAPE, and it is deliberately the dullest one that works:
	//
	//   <log dir>/Rapport.cmd      one verb per line. Written by anything, read
	//                              and TRUNCATED by us.
	//   <log dir>/Rapport.cmd.out  appended: the line, then its answer.
	//
	// No sockets, no ports, no parser worth attacking, and it needs nothing
	// installed. An MCP server on top is a small process that writes one file and
	// tails another.
	//
	// THREADING is the part that matters. A watcher thread does the file IO -- the
	// main thread must never block on a disk read -- and then hands the work to
	// F4SE's task interface, which runs it on the MAIN thread. Touching a form
	// from the watcher thread would be the same class of mistake as calling the
	// Papyrus VM from a worker, which this codebase has a rule against for a
	// reason.
	//
	// It does NOT touch the Papyrus VM, and it is not a second bridge. Rapport
	// verbs read and act on plugin state; the console verb goes through the
	// engine's own console compiler.
	class Mailbox
	{
	public:
		[[nodiscard]] static Mailbox& GetSingleton()
		{
			static Mailbox singleton;
			return singleton;
		}

		// Starts the watcher thread. Does nothing at all unless devMailbox is set
		// in Rapport.ini, so the default build of this plugin has no command
		// channel in any meaningful sense -- the thread does not exist.
		void Start();
		void Stop();

		[[nodiscard]] bool Running() const noexcept { return _running.load(); }

	private:
		Mailbox() = default;
		~Mailbox() { Stop(); }
		Mailbox(const Mailbox&) = delete;
		Mailbox& operator=(const Mailbox&) = delete;

		void Watch();

		// Runs ON THE MAIN THREAD, one line, and returns what to write back.
		[[nodiscard]] std::string Run(const std::string& a_line);

		[[nodiscard]] std::string RunConsole(const std::string& a_command);

		void Reply(std::string_view a_line, std::string_view a_answer) const;

		std::atomic<bool> _running{ false };
		std::thread       _watcher;
		std::filesystem::path _inbox;
		std::filesystem::path _outbox;
	};
}
