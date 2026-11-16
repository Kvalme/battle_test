#include "World.hpp"

#include "AbilityManager.hpp"
#include "ActivationContext.hpp"
#include "Config.hpp"
#include "Damage.hpp"
#include "Effects.hpp"
#include "Entity.hpp"
#include "IRandom.hpp"
#include "Pathfinder.hpp"
#include "Targeting.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sw::core
{

	namespace
	{

		constexpr auto maxHealth = std::numeric_limits<std::uint32_t>::max();
		constexpr auto maxSnapshotBytes = World::maxSnapshotBytes;

		// Cycle detection compares snapshots in memory. Fixed-width numbers and length-prefixed
		// strings keep the encoding unambiguous without using a hash.
		void append(std::string& state, std::uint64_t value)
		{
			state.append(reinterpret_cast<const char*>(&value), sizeof(value));
		}

		bool appendString(std::string& state, std::string_view value)
		{
			if (value.size() > maxSnapshotBytes
				|| state.size() + sizeof(std::uint64_t) + value.size() > maxSnapshotBytes)
			{
				return false;
			}
			append(state, value.size());
			state.append(value);
			return true;
		}

		void append(std::string& state, Position position)
		{
			append(state, position.x);
			append(state, position.y);
		}

	}  // namespace

	// ---- Map and spawn ----

	World::World(IEventSink& events) :
			events_(events)
	{}

	World::World(World&&) noexcept = default;

	World::~World() = default;

	void World::createMap(std::uint32_t width, std::uint32_t height)
	{
		for (const auto& unit : entities_)
		{
			if (unit->alive() && !unit->footprint.fitsAt(unit->position, width, height))
			{
				throw std::invalid_argument("Map cannot contain existing unit: " + std::to_string(unit->id));
			}
		}
		width_ = width;
		height_ = height;
		Event event{};
		event.kind = Event::Kind::MapCreated;
		event.width = width;
		event.height = height;
		emit(std::move(event));
	}

	void World::spawn(EntityId id, Position position, const UnitTemplate& unitTemplate, const PropertyMap& overrides)
	{
		if (std::ranges::any_of(entities_, [id](const auto& unit) { return unit->id == id; }))
		{
			throw std::invalid_argument("Duplicate entity ID: " + std::to_string(id));
		}
		auto unit = std::make_unique<Entity>();
		unit->id = id;
		unit->type = unitTemplate.type;
		unit->position = position;
		unit->footprint = unitTemplate.footprint;
		unit->targetProfile = unitTemplate.targetProfile;
		if (!free(position, *unit))
		{
			throw std::invalid_argument("Unavailable spawn position for unit: " + std::to_string(id));
		}
		unit->properties = resolveUnitProperties(unitTemplate.defaults);
		for (const auto& [key, value] : overrides)
		{
			const auto existing = unit->properties.find(key);
			if (existing == unit->properties.end())
			{
				throw std::invalid_argument("Unknown override property: " + key);
			}
			if (existing->second.index() != value.index())
			{
				throw std::invalid_argument("Wrong type for override property: " + key);
			}
			existing->second = value;
		}
		const auto hp = integerProperty(unit->properties, "hp");
		if (hp <= 0 || hp > maxHealth)
		{
			throw std::invalid_argument("Property out of range: hp");
		}
		unit->hp = static_cast<std::uint32_t>(hp);
		unit->properties.erase("hp");

		// Factories validate the final stats. Publish the unit only after every ability is ready.
		unit->abilities_ = AbilityManager::instance()->createAll(unitTemplate, unit->properties);
		entities_.push_back(std::move(unit));

		Event event{};
		event.kind = Event::Kind::Spawned;
		event.source = id;
		event.position = position;
		event.name = unitTemplate.type;
		emit(std::move(event));
	}

	void World::march(EntityId id, Position destination)
	{
		if (!isAlive(id))
		{
			return;
		}
		auto& unit = mutableEntity(id);
		if (!unit.footprint.fitsAt(destination, width_, height_))
		{
			throw std::invalid_argument("March destination outside the map for unit: " + std::to_string(id));
		}
		unit.march = destination;

		Event event{};
		event.kind = Event::Kind::MarchStarted;
		event.source = id;
		event.position = unit.position;
		event.destination = destination;
		emit(std::move(event));

		if (unit.position == destination)
		{
			move(id, destination);
		}
	}

	// ---- Queries ----

	const Entity& World::entity(EntityId id) const
	{
		const auto found = std::ranges::find_if(entities_, [id](const auto& unit) { return unit->id == id; });
		if (found == entities_.end())
		{
			throw std::out_of_range("Unknown entity: " + std::to_string(id));
		}
		return **found;
	}

	Entity& World::mutableEntity(EntityId id)
	{
		return const_cast<Entity&>(std::as_const(*this).entity(id));
	}

	bool World::isAlive(EntityId id) const
	{
		return std::ranges::any_of(entities_, [id](const auto& unit) { return unit->id == id && unit->alive(); });
	}

	bool World::free(Position position, const Entity& mover) const
	{
		if (!mover.footprint.fitsAt(position, width_, height_))
		{
			return false;
		}
		return mover.targetProfile.layer == Layer::Air
			   || std::ranges::none_of(
					   entities_,
					   [position, &mover](const auto& unit)
					   {
						   return unit->alive() && unit->id != mover.id && unit->targetProfile.layer == Layer::Ground
								  && mover.footprint.distanceTo(position, unit->footprint, unit->position) == 0;
					   });
	}

	std::vector<EntityId> World::targets(EntityId id, const TargetQuery& query) const
	{
		const auto& actor = entity(id);
		std::vector<EntityId> selected;
		if (!actor.alive())
		{
			return selected;
		}

		for (const auto& unit : entities_)
		{
			if (unit->id == id || !unit->alive())
			{
				continue;
			}
			const auto range = actor.footprint.distanceTo(actor.position, unit->footprint, unit->position);
			const auto& profile = unit->targetProfile;
			// Any living ground neighbor can block this query, even if it could not be attacked.
			if (query.noAdjacent && profile.layer == Layer::Ground && range == 1)
			{
				return {};
			}
			const bool ranged = query.kind == AttackKind::Ranged;
			if (profile.invulnerable || (!ranged && !profile.meleeTargetable))
			{
				continue;
			}
			const std::int64_t delta = ranged ? profile.rangeDelta : 0;
			const auto minRange = std::max<std::int64_t>(1, query.minRange - delta);
			const auto maxRange = query.maxRange - delta;
			if (range >= minRange && range <= maxRange)
			{
				selected.push_back(unit->id);
			}
		}
		// Random selection indexes an ID-ordered list so spawn order does not change the mapping.
		std::ranges::sort(selected);
		return selected;
	}

	bool World::finished() const
	{
		return std::ranges::none_of(
				entities_,
				[this](const auto& unit)
				{
					return unit->alive()
						   && (std::ranges::any_of(
									   unit->effects_,
									   [](const auto& effect)
									   { return effect->remaining > 0 && effect->hasPendingWork(); })
							   || std::ranges::any_of(
									   unit->abilities_,
									   [this, &unit](const auto& ability)
									   { return ability->canAct(*this, unit->id); }));
				});
	}

	std::size_t World::unitCount() const
	{
		return entities_.size();
	}

	EntityId World::unitAt(std::size_t index) const
	{
		return entities_.at(index)->id;
	}

	// ---- Movement ----

	std::vector<Position> World::movementPath(EntityId id, unsigned cells) const
	{
		const auto& unit = entity(id);
		if (!unit.alive() || !unit.march || cells == 0)
		{
			return {};
		}
		const auto passable = [this, &unit](Position position)
		{
			return free(position, unit);
		};
		return Pathfinder(width_, height_).find(unit.position, *unit.march, cells, passable);
	}

	std::optional<Position> World::nextPosition(EntityId id, unsigned cells) const
	{
		const auto path = movementPath(id, cells);
		if (path.empty())
		{
			return std::nullopt;
		}
		return path.back();
	}

	void World::move(EntityId id, Position position)
	{
		auto& unit = mutableEntity(id);
		if (!unit.alive() || !free(position, unit))
		{
			return;
		}
		const auto start = std::exchange(unit.position, position);
		finishMovement(unit, start);
	}

	void World::moveAlong(EntityId id, std::span<const Position> path)
	{
		auto& unit = mutableEntity(id);
		if (!unit.alive() || path.empty())
		{
			return;
		}
		const auto start = unit.position;
		// Consecutive origins must be adjacent even when the mover spans several cells.
		// Check occupancy with the mover's full footprint at each step.
		const Footprint cell;
		for (const auto position : path)
		{
			if (cell.distanceTo(unit.position, cell, position) != 1 || !free(position, unit))
			{
				break;
			}
			unit.position = position;
			if (unit.march == position)
			{
				break;
			}
		}
		finishMovement(unit, start);
	}

	void World::finishMovement(Entity& unit, Position start)
	{
		if (unit.position != start)
		{
			Event event{};
			event.kind = Event::Kind::Moved;
			event.source = unit.id;
			event.position = unit.position;
			emit(std::move(event));
		}
		if (unit.march == unit.position)
		{
			unit.march.reset();
			Event event{};
			event.kind = Event::Kind::MarchEnded;
			event.source = unit.id;
			event.position = unit.position;
			emit(std::move(event));
		}
	}

	// ---- Damage and operations ----

	void World::damage(DamageRequest request)
	{
		auto& target = mutableEntity(request.target);
		if (!target.alive() || target.targetProfile.invulnerable || request.amount <= 0)
		{
			return;
		}
		auto amount = request.amount;
		for (const auto& effect : target.effects_)
		{
			if (effect->remaining > 0)
			{
				amount = effect->modifyDamage(request, amount);
			}
		}
		// Apply modifiers before clamping so the event records actual HP lost.
		const auto applied = static_cast<std::uint32_t>(std::clamp<std::int64_t>(amount, 0, target.hp));
		if (applied == 0)
		{
			return;
		}

		beginOperation();
		target.hp -= applied;
		const auto existing = std::ranges::find_if(
				damageEvents_,
				[&request](const Event& event)
				{ return event.source == request.source && event.target == request.target; });
		if (existing == damageEvents_.end())
		{
			Event event{};
			event.kind = Event::Kind::Attacked;
			event.source = request.source;
			event.target = request.target;
			event.damage = applied;
			damageEvents_.push_back(std::move(event));
		}
		else
		{
			existing->damage += applied;
		}
		if (!target.alive())
		{
			target.march.reset();
			deaths_.push_back(target.id);
		}
		endOperation();
	}

	void World::beginOperation()
	{
		++operationDepth_;
	}

	void World::endOperation()
	{
		if (operationDepth_ <= 0)
		{
			throw std::logic_error("No operation to end");
		}
		if (--operationDepth_ != 0)
		{
			return;
		}

		// Nested damage contributes to one event per source and target. Every hit
		// reports the target's final HP for the operation, before any death events.
		retireEffects();
		auto damageEvents = std::exchange(damageEvents_, {});
		auto deaths = std::exchange(deaths_, {});
		for (auto& damageEvent : damageEvents)
		{
			damageEvent.hp = entity(damageEvent.target).hp;
		}
		for (auto& damageEvent : damageEvents)
		{
			emit(std::move(damageEvent));
		}
		for (auto id : deaths)
		{
			Event event{};
			event.kind = Event::Kind::Died;
			event.target = id;
			emit(std::move(event));
		}
	}

	// ---- Effects ----

	void World::applyEffect(std::unique_ptr<IEffect> effect)
	{
		if (!effect || effect->remaining <= 0 || !isAlive(effect->target))
		{
			return;
		}
		if (effect->hasPendingWork() || !effect->cycleState())
		{
			cycleBlockedByEffect_ = true;
		}
		auto& target = mutableEntity(effect->target);
		const auto key = effect->stackKey();
		const auto existing = std::ranges::find_if(
				target.effects_,
				[&key](const auto& current) { return current->remaining > 0 && current->stackKey() == key; });
		// Refresh extends the timer and preserves the original effect instance and its state.
		if (existing != target.effects_.end())
		{
			(*existing)->remaining = std::max((*existing)->remaining, effect->remaining);
			return;
		}

		auto* applied = effect.get();
		target.effects_.push_back(std::move(effect));
		beginOperation();
		applied->onApply(*this);
		endOperation();
	}

	bool World::hasEffect(EntityId id, std::string_view type) const
	{
		if (!isAlive(id))
		{
			return false;
		}
		return std::ranges::any_of(
				entity(id).effects_,
				[type](const auto& effect) { return effect->remaining > 0 && effect->type == type; });
	}

	void World::activateEffects(EntityId id)
	{
		auto& unit = mutableEntity(id);
		if (!unit.alive() || unit.effects_.empty())
		{
			return;
		}

		// Effects applied during a callback join the next activation.
		std::vector<IEffect*> active;
		for (const auto& effect : unit.effects_)
		{
			if (effect->remaining > 0)
			{
				active.push_back(effect.get());
			}
		}
		beginOperation();
		for (auto* effect : active)
		{
			effect->onActivation(*this);
			if (!unit.alive())
			{
				break;
			}
		}
		// Decrement timers together after callbacks so the ordinary countdown cannot expire
		// an effect before another callback in the same activation can observe it.
		if (unit.alive())
		{
			for (auto* effect : active)
			{
				--effect->remaining;
			}
		}
		endOperation();
	}

	void World::retireEffects()
	{
		// Nested damage can kill a target while one of its effects is executing.
		// Retire effects once every callback in the operation has returned.
		for (auto& unit : entities_)
		{
			if (unit->alive())
			{
				std::erase_if(unit->effects_, [](const auto& effect) { return effect->remaining <= 0; });
			}
			else
			{
				unit->effects_.clear();
			}
		}
	}

	// ---- Events ----

	void World::abilityUsed(EntityId id, const std::string& name)
	{
		Event event{};
		event.kind = Event::Kind::AbilityUsed;
		event.source = id;
		event.name = name;
		emit(std::move(event));
	}

	void World::emit(Event event)
	{
		// Stamp at publication so events emitted by nested actions share the current activation tick.
		event.tick = tick_;
		events_.emit(event);
	}

	// ---- Activation and snapshot ----

	void World::activate(EntityId id, IRandom& random)
	{
		auto& unit = mutableEntity(id);
		++tick_;
		// Effects resolve before the unit can act and may kill it during this activation.
		activateEffects(unit.id);
		if (!unit.alive())
		{
			return;
		}

		ActivationContext context{*this, unit.id, random, {}};
		for (const auto& ability : unit.abilities_)
		{
			auto proposal = ability->propose(context);
			if (proposal.status == AbilityProposal::Status::NotApplicable)
			{
				continue;
			}
			if (!proposal.action)
			{
				throw std::logic_error("Ready ability omitted its action");
			}
			beginOperation();
			proposal.action->execute(*this);
			endOperation();
			return;
		}
	}

	bool World::takeCycleBlockedFlag()
	{
		return std::exchange(cycleBlockedByEffect_, false);
	}

	std::optional<std::string> World::snapshot() const
	{
		if (operationDepth_ != 0 || !damageEvents_.empty() || !deaths_.empty())
		{
			return std::nullopt;
		}
		std::string state;
		append(state, width_);
		append(state, height_);
		append(state, entities_.size());
		for (const auto& unit : entities_)
		{
			// ID identifies the immutable properties, footprint and ability
			// configuration, and cannot be reused during the run.
			append(state, unit->id);
			append(state, unit->position);
			append(state, unit->hp);
			append(state, unit->march.has_value());
			if (unit->march)
			{
				append(state, *unit->march);
			}
			if (unit->alive())
			{
				append(state, unit->abilities_.size());
				for (const auto& ability : unit->abilities_)
				{
					const auto privateState = ability->cycleState();
					if (!privateState || !appendString(state, *privateState))
					{
						return std::nullopt;
					}
				}
				append(state, unit->effects_.size());
				for (const auto& effect : unit->effects_)
				{
					if (effect->hasPendingWork())
					{
						return std::nullopt;
					}
					const auto privateState = effect->cycleState();
					if (!privateState || !appendString(state, effect->type))
					{
						return std::nullopt;
					}
					append(state, effect->source);
					append(state, effect->target);
					append(state, static_cast<std::uint64_t>(effect->remaining));
					if (!appendString(state, *privateState))
					{
						return std::nullopt;
					}
				}
			}
			if (state.size() > maxSnapshotBytes)
			{
				return std::nullopt;
			}
		}
		return state;
	}

}  // namespace sw::core
