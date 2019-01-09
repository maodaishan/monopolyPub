#include <eosiolib/eosio.hpp>
#include <eosiolib/transaction.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/system.h>
#include <eosiolib/dispatcher.hpp>
#include <eosiolib/print.hpp>
#include <string>

using namespace eosio;
using std::string;
using eosio::asset;
using eosio::name;
using eosio::contract;
using eosio::symbol;


const static asset ORIGIN_PRICE=asset(10000,symbol("EOS",4));	//original price of any city is 1.0000 EOS.
const static asset ASSET_ZERO=asset(0,symbol("EOS",4));	//zero asset
const static string MEMO_PAY_RENT="pay_rent";
const static string MEMO_BUY_CITY="buy_city";
const static string MEMO_REVEAL="reveal";


CONTRACT monopolygame : public contract{
	public:
	using contract::contract;
	monopolygame(name receiver, name code, eosio::datastream<const char*> ds )
				 :contract(receiver, code, ds), sys_stats(_self, _self.value),citys(_self,_self.value) {}
	ACTION move(name user,uint8_t step){
		require_auth(user);
		eosio_assert(step>=MIN_STEP && step<=MAX_STEP, "invalid step");
		
		//game over check
		eosio_assert(!check_game_over(), "game is over,wtf!");

		//city check and rent check, whether the city exists/has a owner.
		//whether current user needs to pay rent first
		accounts_index account(_self,user.value);
		auto data=account.find(user.value);
		uint8_t new_pos=0;
		bool create_account=false;
		asset rent=ASSET_ZERO;
		if(data==account.end()){
			//first time play.
			create_account=true;
		}else{
			new_pos=data->pos;
			rent=data->rent_quantity;
		}
		eosio_assert(rent==ASSET_ZERO, "need to pay rent before move");
		new_pos=(new_pos+step) % MAX_CITY_COUNT;
		eosio_assert(new_pos>=0 && new_pos<MAX_CITY_COUNT,"invalid city,check step");

		//city exist check
		auto city=citys.find(new_pos);
		name rent_owner;
		if(city==citys.end()){
			rent=ASSET_ZERO;	//new city don't need pay rent.
			rent_owner=_self;
			citys.emplace(_self, [&](auto& c){
				c.city_num=new_pos;
				c.owner=_self;
				c.price=ORIGIN_PRICE;
				c.rent=ASSET_ZERO;
				c.last_modified=0;
				c.label="";
				c.img="";
				c.url="";
				});
		}else{
			if(user!=city->owner){
				rent=city->rent;
				rent_owner=city->owner;
			}else{
				rent=ASSET_ZERO;
				rent_owner=user;
			}
		}
		
		//update data in account table
		if(create_account){
			account.emplace(user, [&](auto& a){
				a.account=user;
				a.pos=new_pos;
				a.rent_quantity=rent;
				a.rent_owner=rent_owner;
				a.to_reveal=ASSET_ZERO;
				});
		}else{
			account.modify(data, user, [&](auto& am){
				am.pos=new_pos;
				am.rent_quantity=rent;
				am.rent_owner=rent_owner;
				});
		}
	}

	ACTION transfer(const name& from,
                            const name& to,
                            const asset& quantity,
                            const string& memo) {
             if(memo==MEMO_REVEAL){
			if(from!=_self){
				return;
			}
             	}else{
			if(from==_self || to!=_self){
				return;
			}
             	}
		//eosio_assert(memo==MEMO_PAY_RENT ||memo==MEMO_BUY_CITY,"only pay_rent and buy_city by transfer");
		if(memo==MEMO_PAY_RENT){
			pay_rent(from,to,quantity,memo);
		}else if(memo==MEMO_BUY_CITY){
			buy_city(from,to,quantity,memo);
		}else if(memo==MEMO_REVEAL){
			reveal(from,to,quantity,memo);
		}else{
			//do nothing
			}
    }

	ACTION setlogo(name user,uint8_t city_num,string label,string img,string url){
		require_auth(user);
		eosio_assert(label.length()<=LABEL_LEN_LIMIT,"input too long");
		eosio_assert(img.length()<=URL_LEN_LIMIT,"input too long");
		eosio_assert(url.length()<=URL_LEN_LIMIT, "input too long");
		auto city=citys.find(city_num);
		eosio_assert(city!=citys.end(),"can't find city in citys table");
		eosio_assert(user==city->owner,"only owner of this city can set its logo");
		citys.modify(city, user, [&](auto& c){
			c.label=label;
			c.img=img;
			c.url=url;
			});
	}
	ACTION testterminate(){
		require_auth(_self);
		uint32_t current=now();
		auto sys=sys_stats.find(_self.value);
		eosio_assert(sys!=sys_stats.end(),"system stats is unavaliable");
		uint32_t last_modified=sys->last_modified;
		eosio_assert(current-last_modified>TIME_LINE,"game not over");
		//game is over.divide the pool
		asset pool=sys->pool;
		asset city_owners_bonus=pool*40/100;//POOL_DIVIDE_CITY_OWNERS;
		asset last_buyer_bonus=pool*35/100;//POOL_DIVIDE_LAST_PLAYER;
		asset* city_weight=(asset*)malloc(sizeof(asset)*MAX_CITY_COUNT);
		name* city_owners=(name*)malloc(sizeof(name)*MAX_CITY_COUNT);
		asset city_price_total=ASSET_ZERO;
		name last_buyer=_self;
		uint32_t last_time_tmp=0;
		uint8_t city_count=0;
		for(uint8_t city_num=0;city_num<MAX_CITY_COUNT;city_num++){
			auto city=citys.find(city_num);
			if(city==citys.end()){
				continue;
			}
			city_weight[city_count]=city->price;
			city_price_total=city_price_total+city->price;
			city_owners[city_count]=city->owner;
			if(city->last_modified>last_time_tmp){
				last_time_tmp=city->last_modified;
				last_buyer=city->owner;
			}
			city_count++;
		}
		
		for(uint8_t i=0;i<city_count;i++){
			accounts_index accounts(_self,city_owners[i].value);
			auto owner=accounts.find(city_owners[i].value);
			eosio_assert(owner!=accounts.end(),"invalid user");
			accounts.modify(owner, city_owners[i], [&](auto& a){
					a.to_reveal+=city_owners_bonus*city_weight[i].amount/city_price_total.amount;
				});
			if(last_buyer==city_owners[i]){
				accounts.modify(owner, city_owners[i], [&](auto& a){
					a.to_reveal+=last_buyer_bonus;
					});
				last_buyer_bonus=ASSET_ZERO;	//avoid award the last_buyer many times.
									//he may has many citys.
			}
		}
		//clear pool.
		sys_stats.modify(sys,_self,[&](auto& s){
			s.pool=ASSET_ZERO;
		});
	}
	ACTION reset(){
		require_auth(_self);
		auto stats=sys_stats.find(_self.value);
		if(stats!=sys_stats.end()){
			sys_stats.erase(stats);
			}
		for(int i=0;i<100;i++){
			auto city=citys.find(i);
			if(city!=citys.end()){
				citys.erase(city);
				}
			}
		}

	private:
	const static uint8_t MAX_CITY_COUNT=100;
	const static uint8_t MIN_STEP=2;	//2 2 dices
	const static uint8_t MAX_STEP=12;	//2 2 dices
	const static uint32_t TIME_LINE=3*24*60*60;	//3 3 days in seconds
	const static unsigned URL_LEN_LIMIT=64;
	const static unsigned LABEL_LEN_LIMIT=32;
	
	TABLE city{
		uint8_t city_num;			//No. of city,0->MAX_CITY_COUNT-1
		name owner;				//who owns the city now,initially it's monopolygame,the game owner.
		asset price;				//current price,next buyer need to pay this price.
		asset rent;				//rent need to pay
		uint32_t last_modified;		//last time bought
		string label;				//description set by current owner
		string img;				//img for this city, set by current owner
		string url;					//url for this city, set by current owner
		uint64_t primary_key() const {return (uint64_t)city_num;}
		};
	typedef eosio::multi_index<"citys"_n, city> citys_index;

	TABLE account_info{
		name account;			//which account this's describing
		uint8_t pos;			//city_num
		asset rent_quantity;	//how much rent need to pay
		name rent_owner;		//pay rent to whom
		asset to_reveal;		//how much money can reveal
		uint64_t primary_key() const {return account.value;}
		};
	typedef eosio::multi_index<"accounts"_n,account_info> accounts_index;

	TABLE stats{
		name account;				//meaning less.just for primary-key.will save _self.
		asset pool;				//lottery pool.last player can take it.
		uint32_t last_modified;		//last time any city owner changed. counts in seconds since 1970.
									//if no city was bourght after 3 days since last bourght,
									//game ends. pool will be assigned to owners.
									//40% to all city owners by their weights.
									//35% to the last player who bourght a city
									//25% to _self

		uint64_t primary_key() const {return account.value;}
		};
	typedef eosio::multi_index<"status"_n,stats> status_index;

	status_index sys_stats;
	citys_index citys;

	void pay_rent(const name& from,
								const name& to,
								const asset& quantity,
								const string& memo){
		accounts_index accounts(_self,from.value);
		auto user=accounts.find(from.value);
		eosio_assert(user!=accounts.end(), "invalid player");
		asset rent=user->rent_quantity;
		asset owner_reveal=ASSET_ZERO;
		name rent_owner=user->rent_owner;
		if(quantity<rent){
			owner_reveal=rent-quantity;
			accounts.modify(user, from, [&](auto& a){
				a.rent_quantity=owner_reveal;
				});
		}else{
			owner_reveal=rent;
			accounts.modify(user, from, [&](auto& a){
				a.rent_quantity=ASSET_ZERO;
				});
			if(quantity>rent){
				//shouldn't happen, user pay more rent,extra money goto pool
				add_pool(quantity-rent);
			}
		}
	
		//mark the rent to rent_owner
		if(owner_reveal>ASSET_ZERO){
			accounts_index owner_index(_self,user->rent_owner.value);
			auto owner=owner_index.find(user->rent_owner.value);
			eosio_assert(owner!=owner_index.end(), "invalid rent owner");
			owner_index.modify(owner, user->rent_owner, [&](auto& o){
				o.to_reveal+=owner_reveal;
				});
		}
	}

	/*user can only buy the city where he/she is.
	if the quantity payed not enough,money goto pool
	if quantity payed more,the extra part goto pool
	if the city already belong to him/her,money goto pool*/
	void buy_city(const name& from,
								const name& to,
								const asset& quantity,
								const string& memo){
		//game over check
		eosio_assert(!check_game_over(), "game is over");
		
		//upgrade city index
		accounts_index accounts(_self,from.value);
		auto user=accounts.find(from.value);
		uint8_t city_num=user->pos;
		auto city=citys.find(city_num);
		eosio_assert(city!=citys.end(),"can't buy city before you move to it");
		name ori_owner=city->owner;
		asset price=city->price;
		eosio_assert(quantity>=price,"need pay at least price of city to buy it");
		uint32_t current=now();
		citys.modify(city, _self, [&](auto& c){
			c.owner=from;
			c.price=(price*5)/4;
			c.rent=price/10;
			c.last_modified=current;
			c.label="";
			c.img="";
			c.url="";
			});
			
		//upgrade accounts index of ori owner,add the city profit to his reveal
		accounts_index accounts_ori(_self,ori_owner.value);
		auto ori_owner_ite=accounts_ori.find(ori_owner.value);
		eosio_assert(ori_owner_ite!=accounts_ori.end(),"invalid original city owner");
		accounts_ori.modify(ori_owner_ite, ori_owner, [&](auto& ori){
			ori.to_reveal+=price*110/125; //original city owner take 110/125 of the price
			});

		//upgrade pool
		add_pool(quantity-price*110/125);
		auto stats=sys_stats.find(_self.value);
		sys_stats.modify(stats, _self, [&](auto& s){
			s.last_modified=current;
			});
	}


	void reveal(const name& from,
								const name& to,
								const asset& quantity,
								const string& memo){
		accounts_index accounts(_self,to.value);
		auto to_itr=accounts.find(to.value);
		eosio_assert(to_itr!=accounts.end(), "invalid player");
		eosio_assert(quantity==to_itr->to_reveal,"reveal quantity error");
		accounts.modify(to_itr, to, [&](auto& u){
			u.to_reveal=ASSET_ZERO;
			});
	}

	bool check_game_over(){
		auto curr_stats=sys_stats.find(_self.value);
		if(curr_stats==sys_stats.end()){
			sys_stats.emplace(_self, [&](auto& stat){
				stat.account=_self;
				stat.pool=ASSET_ZERO;
				stat.last_modified=-1;
			});
			//need add _self to the accounts table.need in buy_city
			accounts_index accounts(_self,_self.value);
			auto self_account=accounts.find(_self.value);
			if(self_account==accounts.end()){
				accounts.emplace(_self, [&](auto& self){
					self.account=_self;
					self.pos=0;
					self.rent_quantity=ASSET_ZERO;
					self.rent_owner=_self;
					self.to_reveal=ASSET_ZERO;
					});
				}
			return false;
		}else{
			uint32_t current=now();
			uint32_t last_modified=curr_stats->last_modified;
			if(last_modified!=-1 && (current-last_modified)>TIME_LINE){
				return true;
			}
			return false;
		}
	}
	
	void add_pool(asset quantity){
		auto curr_stats=sys_stats.find(_self.value);
		//this shouldn't assert
		if(curr_stats==sys_stats.end()){
			sys_stats.emplace(_self, [&](auto& stat){
					stat.account=_self;
					stat.pool=quantity;
					stat.last_modified=-1;
				});
		}else{
			sys_stats.modify(curr_stats, _self, [&](auto& stat){
				stat.pool+=quantity;
				});
		}
	}
};	//end monoplaygame contract

extern "C" {
void apply(uint64_t receiver, uint64_t code, uint64_t action) {
    //monopolygame thiscontract(receiver);

    if ((code == "eosio.token"_n.value) && (action == "transfer"_n.value)) {
        execute_action(name(receiver),name(code), &monopolygame::transfer);
        return;
    }

    if (code != receiver) return;

    switch (action) {
	EOSIO_DISPATCH_HELPER(monopolygame, (move)(setlogo)(testterminate)(reset))
    };
    eosio_exit(0);
}//apply
}//extern "C"
