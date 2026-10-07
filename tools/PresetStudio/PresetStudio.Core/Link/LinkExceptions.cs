using PresetStudio.Schema;

namespace PresetStudio.Link;

public class LinkException : Exception
{
    public LinkException(string message, Exception? inner = null) : base(message, inner) { }
}

/// <summary>The cable was pulled, the port closed, or another program took it.</summary>
public sealed class LinkClosedException : LinkException
{
    public LinkClosedException(string message, Exception? inner = null) : base(message, inner) { }
}

/// <summary>The box did not answer in time.</summary>
public sealed class LinkTimeoutException : LinkException
{
    public LinkTimeoutException(string message) : base(message) { }
}

/// <summary>The box answered, but the editor and the firmware do not fit each other.</summary>
public sealed class IncompatibleDeviceException : LinkException
{
    public IncompatibleDeviceException(string message) : base(message) { }
}

/// <summary>The box refused a request. <see cref="Message"/> says why in plain words.</summary>
public sealed class DeviceRejectedException : LinkException
{
    public byte Command { get; }
    public LinkProtocol.ErrorCode Code { get; }
    public LinkProtocol.RecordProblem Problem { get; }
    /// <summary>The value the box objected to, when it was a single value.</summary>
    public FieldDef? Field { get; }

    public DeviceRejectedException(byte command, LinkProtocol.ErrorCode code, LinkProtocol.RecordProblem problem, FieldDef? field, string message)
        : base(message)
    {
        Command = command;
        Code = code;
        Problem = problem;
        Field = field;
    }

    internal static DeviceRejectedException From(byte command, byte code, byte detail, byte aux, PatchSchema schema, string? presetName)
    {
        var error = (LinkProtocol.ErrorCode)code;
        var problem = (LinkProtocol.RecordProblem)detail;
        FieldDef? field = aux < schema.Fields.Count && problem == LinkProtocol.RecordProblem.BadField ? schema.Fields[aux] : null;
        var who = presetName is null ? "the preset" : $"'{presetName}'";
        var message = error switch
        {
            LinkProtocol.ErrorCode.InvalidRecord => problem switch
            {
                LinkProtocol.RecordProblem.BadName => $"The Pico does not accept the name of {who}.",
                LinkProtocol.RecordProblem.BadPlace => $"The Pico does not accept the page or pad of {who}.",
                LinkProtocol.RecordProblem.BadBase => $"The Pico has no factory preset that {who} could be built on. Its firmware may be older than this editor.",
                LinkProtocol.RecordProblem.EngineNeedsRecipe => $"{who} uses the Recipe engine, but its base preset is not a recipe preset.",
                LinkProtocol.RecordProblem.BadField when field is not null => $"The Pico refused {who}: {field.Label} ({field.Key}) is outside what this sound allows.",
                _ => $"The Pico refused {who} as invalid.",
            },
            LinkProtocol.ErrorCode.NoSpace => "The Pico does not have room for this many presets. Remove some, or free space by deleting unused files from its storage.",
            LinkProtocol.ErrorCode.Storage => "The Pico could not write to its flash storage. Nothing was changed; try again.",
            LinkProtocol.ErrorCode.Busy => "The Pico is busy receiving another transfer. Wait a moment and try again.",
            LinkProtocol.ErrorCode.BadState => "The Pico was not expecting that step of the transfer. Try the transfer again.",
            LinkProtocol.ErrorCode.SlotTaken => $"Two presets in this transfer share a page and pad ({who} is the second one).",
            LinkProtocol.ErrorCode.CountMismatch => "The Pico did not receive as many presets as were announced. Try again.",
            LinkProtocol.ErrorCode.OutOfRange => "The Pico does not have what was asked for (a voice, preset or slot out of range).",
            LinkProtocol.ErrorCode.BadPayload => "The Pico could not understand the request. The editor and the firmware may be different versions.",
            LinkProtocol.ErrorCode.UnknownCommand => "The Pico's firmware does not support Preset Studio. Update it to a version with the preset link.",
            _ => $"The Pico reported error {code}.",
        };
        return new DeviceRejectedException(command, error, problem, field, message);
    }
}
