// zp.hook_bodies - the MovingSteeringWheelSupport / ShakingEngineBlock /
// MovingPlaneParts shared consumer (original sub_10004C70) and the model-registry
// lookup it calls (sub_10003F40). Ordinary C++, so it lives outside the assembly
// unit (P3); the naked detours in source/hook_bodies.cpp call it by name.
//
// The consumer is called between pushad/popad, so it may touch any GPR; the
// lookup and every dereference below are unguarded, exactly like the original.

#include "common.hxx"
#include "common.h"
#include "rebased.h"
#include "body_helpers.h"

#if defined(_WIN32)

namespace
{
// The handling block at CVehicle +0xE18; +0x84 is the steering lock angle the
// tilt divides by. 1.0.8.0 has no CVehicle type in the IDA DB, so the offsets
// are declared here as literal padding instead of a set of named constants.
struct VehicleHandling final
{
	std::byte pad_0000[0x84];
	float steerLock;  // 0x84
};

// The CVehicle fields the three consumers read; the comments are the recovered
// offsets.
struct VehicleView final
{
	std::byte pad_0000[0x2E];
	std::int16_t modelIndex;  // 0x2E: the model index the registry lookup compares
	std::byte pad_0030[0xE18 - 0x30];
	VehicleHandling* handling;  // 0xE18
	std::byte pad_0E1C[0xF64 - 0xE1C];
	std::uint32_t flags;  // 0xF64: 5 selects the plane-part path
	std::byte pad_0F68[0x10C8 - 0xF68];
	float rpm;  // 0x10C8
	std::byte pad_10CC[0x10D8 - 0x10CC];
	float steerAngle;  // 0x10D8
	std::byte pad_10DC[0x1354 - 0x10DC];
	std::uint32_t engineState;  // 0x1354: bit 3 rpm live, bit 4 engine at rest
	std::byte pad_1358[0x1F00 - 0x1358];
	float heave;  // 0x1F00
	float roll;  // 0x1F04
	float pitch;  // 0x1F08
	std::byte pad_1F0C[0x1F3C - 0x1F0C];
	float suspensionTravel;  // 0x1F3C
};

// The nine parts the plane path drives: 89-92 take the suspension travel, 93 the
// heave, and 94-97 the pitch/roll control surfaces.
enum class VehiclePart : int
{
	wheelA = 89,
	wheelB = 91,
	wheelC = 90,
	wheelD = 92,
	heave = 93,
	rollA = 94,
	rollB = 95,
	pitchA = 96,
	pitchB = 97,
};

// sub_10003F40: first entry in the model registry whose name matches. The
// registry's entries are at [registry], stride 0xE0, count +0x14; a candidate's
// name is at entry+0 and its bone id at entry+0x14. Dereferences are unguarded
// like the original's.
const void* VehicleComponent(const void* registry, const char* name) noexcept
{
	constexpr std::size_t kEntryStride = 0xE0;
	constexpr std::size_t kCountOffset = 0x14;
	constexpr std::size_t kBoneOffset = 0x14;
	const auto* entries = *reinterpret_cast<const std::uint8_t* const*>(registry);
	const auto count = *reinterpret_cast<const std::uint16_t*>(
	    static_cast<const std::uint8_t*>(registry) + kCountOffset);
	for (std::uint16_t i = 0; i < count; ++i)
	{
		const auto* entry = entries + static_cast<std::size_t>(i) * kEntryStride;
		const char* candidate = *reinterpret_cast<const char* const*>(entry);
		if (_stricmp(candidate, name) == 0) return entry;
	}
	return nullptr;
}
}  // namespace

extern "C" std::uint32_t abi_steer_body() noexcept
{
	auto* const vehicle = reinterpret_cast<VehicleView*>(
	    static_cast<std::uintptr_t>(zpatch_steer_vehicle));

	using BoneFn = std::uint32_t*(__thiscall*)(void*, std::uint32_t);
	using SetFn = void(__thiscall*)(std::uint32_t*, float);
	using PartFn = void(__thiscall*)(void*, int, int, float, int, int, int);
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	const auto get_bone = reinterpret_cast<BoneFn>(
	    static_cast<std::uintptr_t>(zpatch_pre_fn_59b88));
	const auto set_a = reinterpret_cast<SetFn>(
	    static_cast<std::uintptr_t>(zpatch_pre_fn_5d63c));
	const auto set_b = reinterpret_cast<SetFn>(
	    static_cast<std::uintptr_t>(zpatch_pre_fn_59cdc));
	const auto part_call = reinterpret_cast<PartFn>(
	    static_cast<std::uintptr_t>(zpatch_steer_vtable_fn));

	// sub_10003980: read the current script float. ctx+0x0E holds the slot
	// offset, the slot is at ctx+offset+0x18 and the float at slot+8.
	const auto script_float = [&]() -> float
	{
		constexpr std::size_t kOffsetField = 0x0E;
		constexpr std::size_t kSlotBase = 0x18;
		constexpr std::size_t kFloatOffset = 8;
		const std::uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
		const std::uint32_t offset = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(ctx) + kOffsetField);
		const std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(ctx) + offset + kSlotBase);
		return *reinterpret_cast<const float*>(
		    static_cast<std::uintptr_t>(value) + kFloatOffset);
	};
	const auto model_of = [vehicle]() { return vehicle->modelIndex; };
	const auto identity = [](std::uint32_t* m)
	{
		constexpr std::uint32_t kOne = std::bit_cast<std::uint32_t>(1.0f);
		m[0] = kOne;
		m[1] = 0;
		m[2] = 0;
		m[4] = 0;
		m[5] = kOne;
		m[6] = 0;
		m[8] = 0;
		m[9] = 0;
		m[10] = kOne;
	};
	const auto bone_matrix = [&](std::int16_t model, const char* name) -> std::uint32_t*
	{
		// The ped entry -> +8 -> +0xB4 -> +0xC is the registry chain sub_10004C70
		// walks; every hop is unguarded.
		constexpr std::uint32_t kPedComponent = 0xB4u;
		constexpr std::uint32_t kRegistryLink = 0xCu;
		constexpr std::uint32_t kModelEntryBoneOffset = 0x14;
		const auto base = static_cast<std::uintptr_t>(zpatch_pre_ped_array);
		const std::uint32_t ped = *reinterpret_cast<const std::uint32_t*>(
		    base + 4u * static_cast<std::uint32_t>(model));
		const std::uint32_t p1 = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(ped) + 8);
		const std::uint32_t p2 = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(p1) + kPedComponent);
		const std::uint32_t registry = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(p2) + kRegistryLink);
		const auto* entry = static_cast<const std::uint8_t*>(VehicleComponent(
		    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(registry)), name));
		if (entry == nullptr) return nullptr;  // the original's own `if (v6)` check
		// 0x10004D4A calls get_bone unguarded (it is null only when the loader's
		// preamble missed).
		const std::uint32_t bone =
		    *reinterpret_cast<const std::uint16_t*>(entry + kModelEntryBoneOffset);
		return get_bone(vehicle, bone);
	};

	if (zpatch_flag_steering)
	{
		constexpr double kPi = 3.141592653589793;  // the original's double pi
		const std::int16_t model = model_of();
		const float steer = vehicle->steerAngle;
		// The handling block is read after the steering angle, in the original's
		// order (a null block faults at the same place).
		const VehicleHandling* const handling = vehicle->handling;
		const float lock = handling->steerLock;
		const float turn =
		    static_cast<float>(static_cast<double>(steer / lock) * -kPi);
		float tilt = 0.0f;
		for (int i = 0; i < static_cast<int>(kVehicleLightSlotCount); ++i)
		{
			const std::uint32_t hash = zpatch_wheelangle_models[i];
			if (hash == 0u) break;
			std::int32_t resolved = -1;
			lookup(hash, &resolved);  // sub_10004C70 0x10004CE7: unconditional
			if (resolved == model)
			{
				tilt = static_cast<float>(
				    static_cast<double>(zpatch_wheelangle_angles[i]) * kPi / 180.0);
				break;
			}
		}
		if (model != -1)
		{
			std::uint32_t* matrix = bone_matrix(model, "steeringwheel");
			if (matrix != nullptr)
			{
				identity(matrix);
				if (set_a) set_a(matrix, tilt);
				if (set_b) set_b(matrix, turn);
			}
		}
	}
	if (zpatch_flag_engine)
	{
		constexpr double kRpmScale = 0.75;
		constexpr double kRpmBias = 0.25;
		constexpr double kPhaseEpsilon = 0.01;
		constexpr std::uint32_t kRpmLive = 1u << 3;   // engineState bit 3
		constexpr std::uint32_t kEngineIdle = 1u << 4;  // engineState bit 4
		if (zpatch_engine_last_timestep != script_float())
		{
			const double step = zpatch_engine_descending ? -0.5 : 0.5;
			const float phase = static_cast<float>(
			    static_cast<double>(script_float()) * step +
			    static_cast<double>(zpatch_engine_phase));
			zpatch_engine_phase = phase;
			std::uint8_t direction = zpatch_engine_descending;
			if (static_cast<double>(phase) > kPhaseEpsilon) direction = 1;
			if (static_cast<double>(phase) < -kPhaseEpsilon) direction = 0;
			zpatch_engine_descending = direction;
		}
		const std::int16_t model = model_of();
		if (model != -1)
		{
			std::uint32_t* matrix = bone_matrix(model, "engineblock");
			if (matrix != nullptr)
			{
				identity(matrix);
				const std::uint32_t engine_state = vehicle->engineState;
				float scale = 0.0f;
				if ((engine_state & kRpmLive) != 0u)
					scale = static_cast<float>(
					    static_cast<double>(vehicle->rpm) * kRpmScale + kRpmBias);
				if ((engine_state & kEngineIdle) != 0u) scale = 1.0f;
				if (set_b) set_b(matrix, scale * zpatch_engine_phase);
			}
		}
	}
	if (zpatch_flag_plane && vehicle->flags == 5u && part_call != nullptr)
	{
		constexpr float kTravelScale = 100.0f;
		constexpr double kSurfaceScale = 0.5;
		const float travel = vehicle->suspensionTravel;
		auto drive = [&](VehiclePart part, int count, float value, int a, int b, int c)
		{
			part_call(vehicle, static_cast<int>(part), count, value, a, b, c);
		};
		drive(VehiclePart::wheelA, 4, script_float() * travel * kTravelScale, 0, 0, 0);
		drive(VehiclePart::wheelB, 4, script_float() * travel * kTravelScale, 0, 0, 0);
		drive(VehiclePart::wheelC, 4, script_float() * travel * kTravelScale, 0, 0, 0);
		drive(VehiclePart::wheelD, 4, script_float() * travel * kTravelScale, 0, 0, 0);
		const float pitch = vehicle->pitch;
		const float roll = vehicle->roll;
		const float heave = vehicle->heave;
		drive(VehiclePart::pitchA, 3,
		      static_cast<float>(static_cast<double>(pitch) * kSurfaceScale), 1, 0, 0);
		drive(VehiclePart::pitchB, 3,
		      static_cast<float>(static_cast<double>(pitch) * -kSurfaceScale), 1, 0, 0);
		drive(VehiclePart::rollA, 3,
		      static_cast<float>(static_cast<double>(roll) * -kSurfaceScale), 1, 0, 0);
		drive(VehiclePart::rollB, 3,
		      static_cast<float>(static_cast<double>(roll) * -kSurfaceScale), 1, 0, 0);
		drive(VehiclePart::heave, 5,
		      static_cast<float>(static_cast<double>(heave) * kSurfaceScale), 1, 0, 0);
	}
	// Unconditional tail: snapshot the last script timestep and return its slot.
	{
		constexpr std::size_t kOffsetField = 0x0E;
		constexpr std::size_t kSlotBase = 0x18;
		constexpr std::size_t kFloatOffset = 8;
		const std::uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
		const std::uint32_t offset = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(ctx) + kOffsetField);
		const std::uint32_t value_ptr = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(ctx) + offset + kSlotBase);
		zpatch_engine_last_timestep = *reinterpret_cast<const float*>(
		    static_cast<std::uintptr_t>(value_ptr) + kFloatOffset);
		return value_ptr;
	}
}

#endif  // _WIN32
