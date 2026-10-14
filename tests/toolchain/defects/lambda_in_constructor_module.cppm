// Partition defining the class forward-declared by :declaration. Its in-class constructor holds a
// capturing lambda; GCC 16 writes a module file for this partition that it cannot read back
// ("failed to read compiled module cluster N: Bad file data"). Without the forward declaration in
// another partition, or without the lambda, the module file is fine.
export module gfx.defects.lambda_in_constructor:checked;

import std;
import :declaration;

export namespace defects
{

class Checked
{
public:
    explicit Checked(const Source& source)
    {
        const auto reject = [&source](std::string problem)
        {
            throw std::invalid_argument(std::format("{} (code {})", problem, source.code()));
        };
        if (source.m_code < 0)
        {
            reject("negative code");
        }
    }
};

} // namespace defects
