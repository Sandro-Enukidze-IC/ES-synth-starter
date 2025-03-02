#include <bitset>
#include <Knob.h>

class Knob{
    private:
        int rotation = 0;
        int max = 8; 
        int min = 0; 
        int lastIncrement = 0;

    public:
        std::bitset<2> knobState [];
        
        Knob(std::bitset<2> _knobState, int _max = 8, int _min = 0){
            knobState = _knobState;
            max = _max;
            min = _min;
        }

        void updateValue(std::bitset<2> previousState, std::bitset<2> currentState){
            if(previousState[1] == currentState[1]){ //if B stays the same
                if(previousState[0] != currentState[0]){ //if A flips
                if(previousState[1] == previousState[0]){ //if prev state B = A
                    if(this->rotation < 8){    
                    this->rotation += 1;
                    lastIncrement = 1;
                    }
                }
                else{
                    if(0 < this->rotation){
                    this->rotation -= 1;
                    lastIncrement = -1;
                    }
                }
                }
            }
            else{   //If B flips
                if(previousState[0] != currentState[0]){ //if A flips
                int t = this->rotation + lastIncrement;
                if(-1 < t && t < 9){
                    this->rotation += lastIncrement;
                }  
                }
            }
        }

        void setMax(int newMax){
            this->max = newMax;
        }

        void setMin(int newMin){
            this->max = newMin;
        } 

        void getRotation(){
            return this->rotation;
        }
};