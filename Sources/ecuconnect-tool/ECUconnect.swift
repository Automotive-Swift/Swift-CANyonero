import ArgumentParser

@main
struct ECUconnectCommand: ParsableCommand {

    static var _commandName: String = "ecuconnect-tool"

    static var configuration = CommandConfiguration(
        abstract: "A tool for the ECUconnect OBD2 adapter.",
        version: "0.5.0",
        subcommands: [
            Benchmark.self,
            Config.self,
            Login.self,
            Monitor.self,
            Ping.self,
            Raw.self,
            Term.self,
            Test.self,
            Update.self,
        ]
    )

    @OptionGroup() var options: Options

    struct Options: ParsableArguments {

        @Option(name: [.customShort("u"), .long], help: "The URL to the adapter")
        var url: String = "ecuconnect-l2cap://FFF1:129"
    }
}
