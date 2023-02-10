#include <bullishmint.hpp>

namespace proton
{
  void bullishmint::ontransfer (const name& from, const name& to, const asset& quantity, const string& memo) {
    // Skip if outgoing
    if (from == get_self()) {
      return;
    }

    // Skip if deposit from system accounts
    if (from == "eosio.stake"_n || from == "eosio.ram"_n || from == "eosio"_n) {
      return;
    }

    // Skip if not eosio.token and XPR
    name token_contract = get_first_receiver();
    if (!(token_contract == SYSTEM_TOKEN_CONTRACT && quantity.symbol == XPR_SYMBOL)) {
      return;
    }

    // Validate transfer
    check(to == get_self(), "Invalid Deposit");

    // Calculate RAM bytes
    global_stateram_singleton globalram("eosio"_n, "eosio"_n.value);
    eosio_global_stateram _gstateram = globalram.exists() ? globalram.get() : eosio_global_stateram{};
    
    asset ram_cost = _gstateram.ram_price_per_byte;
    uint64_t ram_bytes = quantity.amount / ram_cost.amount;

    // Determine RAM receiver
    check(memo == "account" || memo == "contract", "tx memo must be 'account' or 'contract'");
    name ram_receiver = memo == "account"
      ? from
      : get_self();

    // Buy the RAM
    buyram_action br_action( SYSTEM_CONTRACT, {get_self(), "active"_n} );
    br_action.send(get_self(), ram_receiver, quantity);

    // Add bytes if contract balance
    if (memo == "contract") {
      auto resource = _resources.find(from.value);
      check(resource != _resources.end(), "resource does not exist, use initstorage first");
      _resources.modify(resource, same_payer, [&](auto& r) {
          r.ram_bytes += ram_bytes;
      });
    }
  }

  void bullishmint::initstorage (
    name account
  ) {
    require_auth(account);

    auto resource = _resources.find(account.value);
    check(resource == _resources.end(), "storage already exists for this account");

    if (resource == _resources.end()) {
      _resources.emplace(get_self(), [&](auto& r) {
        r.account = account;
        r.ram_bytes = FREE_RAM;
      });
    }
  }

  void bullishmint::mintlasttemp (
    name creator,
    name collection_name,
    name schema_name,
    name new_asset_owner,
    atomicassets::ATTRIBUTE_MAP immutable_data,
    atomicassets::ATTRIBUTE_MAP mutable_data,
    uint64_t count
  ) {
    // Verify enough RAM
    require_auth(creator);

    auto resource = _resources.find(creator.value);
    check(resource != _resources.end(), "resource balance not found");
    uint64_t ram_cost = count * ASSET_RAM_COST_BYTES;
    check(resource->ram_bytes >= ram_cost, "Need more blockchain storage. Current: " + to_string(resource->ram_bytes) + " bytes, Required: " + to_string(ram_cost) + " bytes");

    _resources.modify(resource, same_payer, [&](auto& r) {
      r.ram_bytes -= ram_cost;
    });

    if (resource->ram_bytes == 0) {
      _resources.erase(resource);
    }

    // Further validation
    auto collection = atomicassets::collections.find(collection_name.value);
    check(collection != atomicassets::collections.end(), "no collection found with name");

    auto creator_authorized = find(collection->authorized_accounts.begin(), collection->authorized_accounts.end(), creator);
    check(creator_authorized != collection->authorized_accounts.end(), "creator is not authorized");

    auto self_authorized = find(collection->authorized_accounts.begin(), collection->authorized_accounts.end(), get_self());
    check(self_authorized != collection->authorized_accounts.end(), "contract is not authorized");

    atomicassets::templates_t templates = atomicassets::get_templates(collection_name);
    check(templates.begin() != templates.end(), "no templates found for collection");

    auto relevant_template = templates.end();
    relevant_template--;

    for (int i = 0; i < count; ++i) {
      bullishmint::mint_action m_action( atomicassets::ATOMICASSETS_ACCOUNT, {get_self(), "active"_n} );
      m_action.send(
        get_self(),
        collection_name,
        schema_name,
        relevant_template->template_id,
        new_asset_owner,
        immutable_data,
        mutable_data,
        vector<asset>{} // token to back
      );
    }
  }
} // namepsace contract
